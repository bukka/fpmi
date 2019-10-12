
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <unistd.h>
#include <errno.h>
#include <stdlib.h> /* for putenv */
#include <string.h>

#include <php.h>

#include "fpmi.h"
#include "fpmi_process_ctl.h"
#include "fpmi_events.h"
#include "fpmi_cleanup.h"
#include "fpmi_stdio.h"
#include "fpmi_signals.h"
#include "fpmi_children.h"
#include "zlog.h"
#include "fpmi_clock.h"
#include "fpmi_log.h"

#include "events/select.h"
#include "events/poll.h"
#include "events/epoll.h"
#include "events/devpoll.h"
#include "events/port.h"
#include "events/kqueue.h"

#ifdef HAVE_SYSTEMD
#include "fpmi_systemd.h"
#endif

#define fpmi_event_set_timeout(ev, now) timeradd(&(now), &(ev)->frequency, &(ev)->timeout);

static void fpmi_event_cleanup(int which, void *arg);
static void fpmi_got_signal(struct fpmi_event_s *ev, short which, void *arg);
static struct fpmi_event_s *fpmi_event_queue_isset(struct fpmi_event_queue_s *queue, struct fpmi_event_s *ev);
static int fpmi_event_queue_add(struct fpmi_event_queue_s **queue, struct fpmi_event_s *ev);
static int fpmi_event_queue_del(struct fpmi_event_queue_s **queue, struct fpmi_event_s *ev);
static void fpmi_event_queue_destroy(struct fpmi_event_queue_s **queue);

static struct fpmi_event_module_s *module;
static struct fpmi_event_queue_s *fpmi_event_queue_timer = NULL;
static struct fpmi_event_queue_s *fpmi_event_queue_fd = NULL;

static void fpmi_event_cleanup(int which, void *arg) /* {{{ */
{
	fpmi_event_queue_destroy(&fpmi_event_queue_timer);
	fpmi_event_queue_destroy(&fpmi_event_queue_fd);
}
/* }}} */

static void fpmi_got_signal(struct fpmi_event_s *ev, short which, void *arg) /* {{{ */
{
	char c;
	int res, ret;
	int fd = ev->fd;

	do {
		do {
			res = read(fd, &c, 1);
		} while (res == -1 && errno == EINTR);

		if (res <= 0) {
			if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
				zlog(ZLOG_SYSERROR, "unable to read from the signal pipe");
			}
			return;
		}

		switch (c) {
			case 'C' :                  /* SIGCHLD */
				zlog(ZLOG_DEBUG, "received SIGCHLD");
				fpmi_children_bury();
				break;
			case 'I' :                  /* SIGINT  */
				zlog(ZLOG_DEBUG, "received SIGINT");
				zlog(ZLOG_NOTICE, "Terminating ...");
				fpmi_pctl(FPMI_PCTL_STATE_TERMINATING, FPMI_PCTL_ACTION_SET);
				break;
			case 'T' :                  /* SIGTERM */
				zlog(ZLOG_DEBUG, "received SIGTERM");
				zlog(ZLOG_NOTICE, "Terminating ...");
				fpmi_pctl(FPMI_PCTL_STATE_TERMINATING, FPMI_PCTL_ACTION_SET);
				break;
			case 'Q' :                  /* SIGQUIT */
				zlog(ZLOG_DEBUG, "received SIGQUIT");
				zlog(ZLOG_NOTICE, "Finishing ...");
				fpmi_pctl(FPMI_PCTL_STATE_FINISHING, FPMI_PCTL_ACTION_SET);
				break;
			case '1' :                  /* SIGUSR1 */
				zlog(ZLOG_DEBUG, "received SIGUSR1");
				if (0 == fpmi_stdio_open_error_log(1)) {
					zlog(ZLOG_NOTICE, "error log file re-opened");
				} else {
					zlog(ZLOG_ERROR, "unable to re-opened error log file");
				}

				ret = fpmi_log_open(1);
				if (ret == 0) {
					zlog(ZLOG_NOTICE, "access log file re-opened");
				} else if (ret == -1) {
					zlog(ZLOG_ERROR, "unable to re-opened access log file");
				}
				/* else no access log are set */

				break;
			case '2' :                  /* SIGUSR2 */
				zlog(ZLOG_DEBUG, "received SIGUSR2");
				zlog(ZLOG_NOTICE, "Reloading in progress ...");
				fpmi_pctl(FPMI_PCTL_STATE_RELOADING, FPMI_PCTL_ACTION_SET);
				break;
		}

		if (fpmi_globals.is_child) {
			break;
		}
	} while (1);
	return;
}
/* }}} */

static struct fpmi_event_s *fpmi_event_queue_isset(struct fpmi_event_queue_s *queue, struct fpmi_event_s *ev) /* {{{ */
{
	if (!ev) {
		return NULL;
	}

	while (queue) {
		if (queue->ev == ev) {
			return ev;
		}
		queue = queue->next;
	}

	return NULL;
}
/* }}} */

static int fpmi_event_queue_add(struct fpmi_event_queue_s **queue, struct fpmi_event_s *ev) /* {{{ */
{
	struct fpmi_event_queue_s *elt;

	if (!queue || !ev) {
		return -1;
	}

	if (fpmi_event_queue_isset(*queue, ev)) {
		return 0;
	}

	if (!(elt = malloc(sizeof(struct fpmi_event_queue_s)))) {
		zlog(ZLOG_SYSERROR, "Unable to add the event to queue: malloc() failed");
		return -1;
	}
	elt->prev = NULL;
	elt->next = NULL;
	elt->ev = ev;

	if (*queue) {
		(*queue)->prev = elt;
		elt->next = *queue;
	}
	*queue = elt;

	/* ask the event module to add the fd from its own queue */
	if (*queue == fpmi_event_queue_fd && module->add) {
		module->add(ev);
	}

	return 0;
}
/* }}} */

static int fpmi_event_queue_del(struct fpmi_event_queue_s **queue, struct fpmi_event_s *ev) /* {{{ */
{
	struct fpmi_event_queue_s *q;
	if (!queue || !ev) {
		return -1;
	}
	q = *queue;
	while (q) {
		if (q->ev == ev) {
			if (q->prev) {
				q->prev->next = q->next;
			}
			if (q->next) {
				q->next->prev = q->prev;
			}
			if (q == *queue) {
				*queue = q->next;
				if (*queue) {
					(*queue)->prev = NULL;
				}
			}

			/* ask the event module to remove the fd from its own queue */
			if (*queue == fpmi_event_queue_fd && module->remove) {
				module->remove(ev);
			}

			free(q);
			return 0;
		}
		q = q->next;
	}
	return -1;
}
/* }}} */

static void fpmi_event_queue_destroy(struct fpmi_event_queue_s **queue) /* {{{ */
{
	struct fpmi_event_queue_s *q, *tmp;

	if (!queue) {
		return;
	}

	if (*queue == fpmi_event_queue_fd && module->clean) {
		module->clean();
	}

	q = *queue;
	while (q) {
		tmp = q;
		q = q->next;
		/* q->prev = NULL */
		free(tmp);
	}
	*queue = NULL;
}
/* }}} */

int fpmi_event_pre_init(char *machanism) /* {{{ */
{
	/* kqueue */
	module = fpmi_event_kqueue_module();
	if (module) {
		if (!machanism || strcasecmp(module->name, machanism) == 0) {
			return 0;
		}
	}

	/* port */
	module = fpmi_event_port_module();
	if (module) {
		if (!machanism || strcasecmp(module->name, machanism) == 0) {
			return 0;
		}
	}

	/* epoll */
	module = fpmi_event_epoll_module();
	if (module) {
		if (!machanism || strcasecmp(module->name, machanism) == 0) {
			return 0;
		}
	}

	/* /dev/poll */
	module = fpmi_event_devpoll_module();
	if (module) {
		if (!machanism || strcasecmp(module->name, machanism) == 0) {
			return 0;
		}
	}

	/* poll */
	module = fpmi_event_poll_module();
	if (module) {
		if (!machanism || strcasecmp(module->name, machanism) == 0) {
			return 0;
		}
	}

	/* select */
	module = fpmi_event_select_module();
	if (module) {
		if (!machanism || strcasecmp(module->name, machanism) == 0) {
			return 0;
		}
	}

	if (machanism) {
		zlog(ZLOG_ERROR, "event mechanism '%s' is not available on this system", machanism);
	} else {
		zlog(ZLOG_ERROR, "unable to find a suitable event mechanism on this system");
	}
	return -1;
}
/* }}} */

const char *fpmi_event_machanism_name() /* {{{ */
{
	return module ? module->name : NULL;
}
/* }}} */

int fpmi_event_support_edge_trigger() /* {{{ */
{
	return module ? module->support_edge_trigger : 0;
}
/* }}} */

int fpmi_event_init_main() /* {{{ */
{
	struct fpmi_worker_pool_s *wp;
	int max;

	if (!module) {
		zlog(ZLOG_ERROR, "no event module found");
		return -1;
	}

	if (!module->wait) {
		zlog(ZLOG_ERROR, "Incomplete event implementation. Please open a bug report on https://bugs.php.net.");
		return -1;
	}

	/* count the max number of necessary fds for polling */
	max = 1; /* only one FD is necessary at startup for the master process signal pipe */
	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		if (!wp->config) continue;
		if (wp->config->catch_workers_output && wp->config->pm_max_children > 0) {
			max += (wp->config->pm_max_children * 2);
		}
	}

	if (module->init(max) < 0) {
		zlog(ZLOG_ERROR, "Unable to initialize the event module %s", module->name);
		return -1;
	}

	zlog(ZLOG_DEBUG, "event module is %s and %d fds have been reserved", module->name, max);

	if (0 > fpmi_cleanup_add(FPMI_CLEANUP_ALL, fpmi_event_cleanup, NULL)) {
		return -1;
	}
	return 0;
}
/* }}} */

void fpmi_event_loop(int err) /* {{{ */
{
	static struct fpmi_event_s signal_fd_event;

	/* sanity check */
	if (fpmi_globals.parent_pid != getpid()) {
		return;
	}

	fpmi_event_set(&signal_fd_event, fpmi_signals_get_fd(), FPMI_EV_READ, &fpmi_got_signal, NULL);
	fpmi_event_add(&signal_fd_event, 0);

	/* add timers */
	if (fpmi_globals.heartbeat > 0) {
		fpmi_pctl_heartbeat(NULL, 0, NULL);
	}

	if (!err) {
		fpmi_pctl_perform_idle_server_maintenance_heartbeat(NULL, 0, NULL);

		zlog(ZLOG_DEBUG, "%zu bytes have been reserved in SHM", fpmi_shm_get_size_allocated());
		zlog(ZLOG_NOTICE, "ready to handle connections");

#ifdef HAVE_SYSTEMD
		fpmi_systemd_heartbeat(NULL, 0, NULL);
#endif
	}

	while (1) {
		struct fpmi_event_queue_s *q, *q2;
		struct timeval ms;
		struct timeval tmp;
		struct timeval now;
		unsigned long int timeout;
		int ret;

		/* sanity check */
		if (fpmi_globals.parent_pid != getpid()) {
			return;
		}

		fpmi_clock_get(&now);
		timerclear(&ms);

		/* search in the timeout queue for the next timer to trigger */
		q = fpmi_event_queue_timer;
		while (q) {
			if (!timerisset(&ms)) {
				ms = q->ev->timeout;
			} else {
				if (timercmp(&q->ev->timeout, &ms, <)) {
					ms = q->ev->timeout;
				}
			}
			q = q->next;
		}

		/* 1s timeout if none has been set */
		if (!timerisset(&ms) || timercmp(&ms, &now, <) || timercmp(&ms, &now, ==)) {
			timeout = 1000;
		} else {
			timersub(&ms, &now, &tmp);
			timeout = (tmp.tv_sec * 1000) + (tmp.tv_usec / 1000) + 1;
		}

		ret = module->wait(fpmi_event_queue_fd, timeout);

		/* is a child, nothing to do here */
		if (ret == -2) {
			return;
		}

		if (ret > 0) {
			zlog(ZLOG_DEBUG, "event module triggered %d events", ret);
		}

		/* trigger timers */
		q = fpmi_event_queue_timer;
		while (q) {
			struct fpmi_event_queue_s *next = q->next;
			fpmi_clock_get(&now);
			if (q->ev) {
				if (timercmp(&now, &q->ev->timeout, >) || timercmp(&now, &q->ev->timeout, ==)) {
					struct fpmi_event_s *ev = q->ev;
					if (ev->flags & FPMI_EV_PERSIST) {
						fpmi_event_set_timeout(ev, now);
					} else {
						/* Delete the event. Make sure this happens before it is fired,
						 * so that the event callback may register the same timer again. */
						q2 = q;
						if (q->prev) {
							q->prev->next = q->next;
						}
						if (q->next) {
							q->next->prev = q->prev;
						}
						if (q == fpmi_event_queue_timer) {
							fpmi_event_queue_timer = q->next;
							if (fpmi_event_queue_timer) {
								fpmi_event_queue_timer->prev = NULL;
							}
						}
						free(q2);
					}

					fpmi_event_fire(ev);

					/* sanity check */
					if (fpmi_globals.parent_pid != getpid()) {
						return;
					}
				}
			}
			q = next;
		}
	}
}
/* }}} */

void fpmi_event_fire(struct fpmi_event_s *ev) /* {{{ */
{
	if (!ev || !ev->callback) {
		return;
	}

	(*ev->callback)( (struct fpmi_event_s *) ev, ev->which, ev->arg);
}
/* }}} */

int fpmi_event_set(struct fpmi_event_s *ev, int fd, int flags, void (*callback)(struct fpmi_event_s *, short, void *), void *arg) /* {{{ */
{
	if (!ev || !callback || fd < -1) {
		return -1;
	}
	memset(ev, 0, sizeof(struct fpmi_event_s));
	ev->fd = fd;
	ev->callback = callback;
	ev->arg = arg;
	ev->flags = flags;
	return 0;
}
/* }}} */

int fpmi_event_add(struct fpmi_event_s *ev, unsigned long int frequency) /* {{{ */
{
	struct timeval now;
	struct timeval tmp;

	if (!ev) {
		return -1;
	}

	ev->index = -1;

	/* it's a triggered event on incoming data */
	if (ev->flags & FPMI_EV_READ) {
		ev->which = FPMI_EV_READ;
		if (fpmi_event_queue_add(&fpmi_event_queue_fd, ev) != 0) {
			return -1;
		}
		return 0;
	}

	/* it's a timer event */
	ev->which = FPMI_EV_TIMEOUT;

	fpmi_clock_get(&now);
	if (frequency >= 1000) {
		tmp.tv_sec = frequency / 1000;
		tmp.tv_usec = (frequency % 1000) * 1000;
	} else {
		tmp.tv_sec = 0;
		tmp.tv_usec = frequency * 1000;
	}
	ev->frequency = tmp;
	fpmi_event_set_timeout(ev, now);

	if (fpmi_event_queue_add(&fpmi_event_queue_timer, ev) != 0) {
		return -1;
	}

	return 0;
}
/* }}} */

int fpmi_event_del(struct fpmi_event_s *ev) /* {{{ */
{
	if (ev->index >= 0 && fpmi_event_queue_del(&fpmi_event_queue_fd, ev) != 0) {
		return -1;
	}

	if (ev->index < 0 && fpmi_event_queue_del(&fpmi_event_queue_timer, ev) != 0) {
		return -1;
	}

	return 0;
}
/* }}} */

