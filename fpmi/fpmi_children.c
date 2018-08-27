
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "fpmi.h"
#include "fpmi_children.h"
#include "fpmi_signals.h"
#include "fpmi_worker_pool.h"
#include "fpmi_sockets.h"
#include "fpmi_process_ctl.h"
#include "fpmi_php.h"
#include "fpmi_conf.h"
#include "fpmi_cleanup.h"
#include "fpmi_events.h"
#include "fpmi_clock.h"
#include "fpmi_stdio.h"
#include "fpmi_unix.h"
#include "fpmi_env.h"
#include "fpmi_scoreboard.h"
#include "fpmi_status.h"
#include "fpmi_log.h"

#include "zlog.h"

static time_t *last_faults;
static int fault;

static void fpmi_children_cleanup(int which, void *arg) /* {{{ */
{
	free(last_faults);
}
/* }}} */

static struct fpmi_child_s *fpmi_child_alloc() /* {{{ */
{
	struct fpmi_child_s *ret;

	ret = malloc(sizeof(struct fpmi_child_s));

	if (!ret) {
		return 0;
	}

	memset(ret, 0, sizeof(*ret));
	ret->scoreboard_i = -1;
	return ret;
}
/* }}} */

static void fpmi_child_free(struct fpmi_child_s *child) /* {{{ */
{
	free(child);
}
/* }}} */

static void fpmi_child_close(struct fpmi_child_s *child, int in_event_loop) /* {{{ */
{
	if (child->fd_stdout != -1) {
		if (in_event_loop) {
			fpmi_event_fire(&child->ev_stdout);
		}
		if (child->fd_stdout != -1) {
			close(child->fd_stdout);
		}
	}

	if (child->fd_stderr != -1) {
		if (in_event_loop) {
			fpmi_event_fire(&child->ev_stderr);
		}
		if (child->fd_stderr != -1) {
			close(child->fd_stderr);
		}
	}

	fpmi_child_free(child);
}
/* }}} */

static void fpmi_child_link(struct fpmi_child_s *child) /* {{{ */
{
	struct fpmi_worker_pool_s *wp = child->wp;

	++wp->running_children;
	++fpmi_globals.running_children;

	child->next = wp->children;
	if (child->next) {
		child->next->prev = child;
	}
	child->prev = 0;
	wp->children = child;
}
/* }}} */

static void fpmi_child_unlink(struct fpmi_child_s *child) /* {{{ */
{
	--child->wp->running_children;
	--fpmi_globals.running_children;

	if (child->prev) {
		child->prev->next = child->next;
	} else {
		child->wp->children = child->next;
	}

	if (child->next) {
		child->next->prev = child->prev;
	}
}
/* }}} */

static struct fpmi_child_s *fpmi_child_find(pid_t pid) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;
	struct fpmi_child_s *child = 0;

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {

		for (child = wp->children; child; child = child->next) {
			if (child->pid == pid) {
				break;
			}
		}

		if (child) break;
	}

	if (!child) {
		return 0;
	}

	return child;
}
/* }}} */

static void fpmi_child_init(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	fpmi_globals.max_requests = wp->config->pm_max_requests;
	fpmi_globals.listening_socket = dup(wp->listening_socket);

	if (0 > fpmi_stdio_init_child(wp)  ||
	    0 > fpmi_log_init_child(wp)    ||
	    0 > fpmi_status_init_child(wp) ||
	    0 > fpmi_unix_init_child(wp)   ||
	    0 > fpmi_signals_init_child()  ||
	    0 > fpmi_env_init_child(wp)    ||
	    0 > fpmi_php_init_child(wp)) {

		zlog(ZLOG_ERROR, "[pool %s] child failed to initialize", wp->config->name);
		exit(FPMI_EXIT_SOFTWARE);
	}
}
/* }}} */

int fpmi_children_free(struct fpmi_child_s *child) /* {{{ */
{
	struct fpmi_child_s *next;

	for (; child; child = next) {
		next = child->next;
		fpmi_child_close(child, 0 /* in_event_loop */);
	}

	return 0;
}
/* }}} */

void fpmi_children_bury() /* {{{ */
{
	int status;
	pid_t pid;
	struct fpmi_child_s *child;

	while ( (pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		char buf[128];
		int severity = ZLOG_NOTICE;
		int restart_child = 1;

		child = fpmi_child_find(pid);

		if (WIFEXITED(status)) {

			snprintf(buf, sizeof(buf), "with code %d", WEXITSTATUS(status));

			/* if it's been killed because of dynamic process management
			 * don't restart it automatically
			 */
			if (child && child->idle_kill) {
				restart_child = 0;
			}

			if (WEXITSTATUS(status) != FPMI_EXIT_OK) {
				severity = ZLOG_WARNING;
			}

		} else if (WIFSIGNALED(status)) {
			const char *signame = fpmi_signal_names[WTERMSIG(status)];
			const char *have_core = WCOREDUMP(status) ? " - core dumped" : "";

			if (signame == NULL) {
				signame = "";
			}

			snprintf(buf, sizeof(buf), "on signal %d (%s%s)", WTERMSIG(status), signame, have_core);

			/* if it's been killed because of dynamic process management
			 * don't restart it automatically
			 */
			if (child && child->idle_kill && WTERMSIG(status) == SIGQUIT) {
				restart_child = 0;
			}

			if (WTERMSIG(status) != SIGQUIT) { /* possible request loss */
				severity = ZLOG_WARNING;
			}
		} else if (WIFSTOPPED(status)) {

			zlog(ZLOG_NOTICE, "child %d stopped for tracing", (int) pid);

			if (child && child->tracer) {
				child->tracer(child);
			}

			continue;
		}

		if (child) {
			struct fpmi_worker_pool_s *wp = child->wp;
			struct timeval tv1, tv2;

			fpmi_child_unlink(child);

			fpmi_scoreboard_proc_free(wp->scoreboard, child->scoreboard_i);

			fpmi_clock_get(&tv1);

			timersub(&tv1, &child->started, &tv2);

			if (restart_child) {
				if (!fpmi_pctl_can_spawn_children()) {
					severity = ZLOG_DEBUG;
				}
				zlog(severity, "[pool %s] child %d exited %s after %ld.%06d seconds from start", child->wp->config->name, (int) pid, buf, tv2.tv_sec, (int) tv2.tv_usec);
			} else {
				zlog(ZLOG_DEBUG, "[pool %s] child %d has been killed by the process management after %ld.%06d seconds from start", child->wp->config->name, (int) pid, tv2.tv_sec, (int) tv2.tv_usec);
			}

			fpmi_child_close(child, 1 /* in event_loop */);

			fpmi_pctl_child_exited();

			if (last_faults && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS)) {
				time_t now = tv1.tv_sec;
				int restart_condition = 1;
				int i;

				last_faults[fault++] = now;

				if (fault == fpmi_global_config.emergency_restart_threshold) {
					fault = 0;
				}

				for (i = 0; i < fpmi_global_config.emergency_restart_threshold; i++) {
					if (now - last_faults[i] > fpmi_global_config.emergency_restart_interval) {
						restart_condition = 0;
						break;
					}
				}

				if (restart_condition) {

					zlog(ZLOG_WARNING, "failed processes threshold (%d in %d sec) is reached, initiating reload", fpmi_global_config.emergency_restart_threshold, fpmi_global_config.emergency_restart_interval);

					fpmi_pctl(FPMI_PCTL_STATE_RELOADING, FPMI_PCTL_ACTION_SET);
				}
			}

			if (restart_child) {
				fpmi_children_make(wp, 1 /* in event loop */, 1, 0);

				if (fpmi_globals.is_child) {
					break;
				}
			}
		} else {
			zlog(ZLOG_ALERT, "oops, unknown child (%d) exited %s. Please open a bug report (https://bugs.php.net).", pid, buf);
		}
	}
}
/* }}} */

static struct fpmi_child_s *fpmi_resources_prepare(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	struct fpmi_child_s *c;

	c = fpmi_child_alloc();

	if (!c) {
		zlog(ZLOG_ERROR, "[pool %s] unable to malloc new child", wp->config->name);
		return 0;
	}

	c->wp = wp;
	c->fd_stdout = -1; c->fd_stderr = -1;

	if (0 > fpmi_stdio_prepare_pipes(c)) {
		fpmi_child_free(c);
		return 0;
	}

	if (0 > fpmi_scoreboard_proc_alloc(wp->scoreboard, &c->scoreboard_i)) {
		fpmi_stdio_discard_pipes(c);
		fpmi_child_free(c);
		return 0;
	}

	return c;
}
/* }}} */

static void fpmi_resources_discard(struct fpmi_child_s *child) /* {{{ */
{
	fpmi_scoreboard_proc_free(child->wp->scoreboard, child->scoreboard_i);
	fpmi_stdio_discard_pipes(child);
	fpmi_child_free(child);
}
/* }}} */

static void fpmi_child_resources_use(struct fpmi_child_s *child) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;
	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		if (wp == child->wp) {
			continue;
		}
		fpmi_scoreboard_free(wp->scoreboard);
	}

	fpmi_scoreboard_child_use(child->wp->scoreboard, child->scoreboard_i, getpid());
	fpmi_stdio_child_use_pipes(child);
	fpmi_child_free(child);
}
/* }}} */

static void fpmi_parent_resources_use(struct fpmi_child_s *child) /* {{{ */
{
	fpmi_stdio_parent_use_pipes(child);
	fpmi_child_link(child);
}
/* }}} */

int fpmi_children_make(struct fpmi_worker_pool_s *wp, int in_event_loop, int nb_to_spawn, int is_debug) /* {{{ */
{
	pid_t pid;
	struct fpmi_child_s *child;
	int max;
	static int warned = 0;

	if (wp->config->pm == PM_STYLE_DYNAMIC) {
		if (!in_event_loop) { /* starting */
			max = wp->config->pm_start_servers;
		} else {
			max = wp->running_children + nb_to_spawn;
		}
	} else if (wp->config->pm == PM_STYLE_ONDEMAND) {
		if (!in_event_loop) { /* starting */
			max = 0; /* do not create any child at startup */
		} else {
			max = wp->running_children + nb_to_spawn;
		}
	} else { /* PM_STYLE_STATIC */
		max = wp->config->pm_max_children;
	}

	/*
	 * fork children while:
	 *   - fpmi_pctl_can_spawn_children : FPMI is running in a NORMAL state (aka not restart, stop or reload)
	 *   - wp->running_children < max  : there is less than the max process for the current pool
	 *   - (fpmi_global_config.process_max < 1 || fpmi_globals.running_children < fpmi_global_config.process_max):
	 *     if fpmi_global_config.process_max is set, FPMI has not fork this number of processes (globaly)
	 */
	while (fpmi_pctl_can_spawn_children() && wp->running_children < max && (fpmi_global_config.process_max < 1 || fpmi_globals.running_children < fpmi_global_config.process_max)) {

		warned = 0;
		child = fpmi_resources_prepare(wp);

		if (!child) {
			return 2;
		}

		pid = fork();

		switch (pid) {

			case 0 :
				fpmi_child_resources_use(child);
				fpmi_globals.is_child = 1;
				fpmi_child_init(wp);
				return 0;

			case -1 :
				zlog(ZLOG_SYSERROR, "fork() failed");

				fpmi_resources_discard(child);
				return 2;

			default :
				child->pid = pid;
				fpmi_clock_get(&child->started);
				fpmi_parent_resources_use(child);

				zlog(is_debug ? ZLOG_DEBUG : ZLOG_NOTICE, "[pool %s] child %d started", wp->config->name, (int) pid);
		}

	}

	if (!warned && fpmi_global_config.process_max > 0 && fpmi_globals.running_children >= fpmi_global_config.process_max) {
               if (wp->running_children < max) {
                       warned = 1;
                       zlog(ZLOG_WARNING, "The maximum number of processes has been reached. Please review your configuration and consider raising 'process.max'");
               }
	}

	return 1; /* we are done */
}
/* }}} */

int fpmi_children_create_initial(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	if (wp->config->pm == PM_STYLE_ONDEMAND) {
		wp->ondemand_event = (struct fpmi_event_s *)malloc(sizeof(struct fpmi_event_s));

		if (!wp->ondemand_event) {
			zlog(ZLOG_ERROR, "[pool %s] unable to malloc the ondemand socket event", wp->config->name);
			// FIXME handle crash
			return 1;
		}

		memset(wp->ondemand_event, 0, sizeof(struct fpmi_event_s));
		fpmi_event_set(wp->ondemand_event, wp->listening_socket, FPMI_EV_READ | FPMI_EV_EDGE, fpmi_pctl_on_socket_accept, wp);
		wp->socket_event_set = 1;
		fpmi_event_add(wp->ondemand_event, 0);

		return 1;
	}
	return fpmi_children_make(wp, 0 /* not in event loop yet */, 0, 1);
}
/* }}} */

int fpmi_children_init_main() /* {{{ */
{
	if (fpmi_global_config.emergency_restart_threshold &&
		fpmi_global_config.emergency_restart_interval) {

		last_faults = malloc(sizeof(time_t) * fpmi_global_config.emergency_restart_threshold);

		if (!last_faults) {
			return -1;
		}

		memset(last_faults, 0, sizeof(time_t) * fpmi_global_config.emergency_restart_threshold);
	}

	if (0 > fpmi_cleanup_add(FPMI_CLEANUP_ALL, fpmi_children_cleanup, 0)) {
		return -1;
	}

	return 0;
}
/* }}} */

