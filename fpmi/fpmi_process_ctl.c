
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "fpmi.h"
#include "fpmi_clock.h"
#include "fpmi_children.h"
#include "fpmi_signals.h"
#include "fpmi_events.h"
#include "fpmi_process_ctl.h"
#include "fpmi_cleanup.h"
#include "fpmi_request.h"
#include "fpmi_worker_pool.h"
#include "fpmi_scoreboard.h"
#include "fpmi_sockets.h"
#include "zlog.h"


static int fpmi_state = FPMI_PCTL_STATE_NORMAL;
static int fpmi_signal_sent = 0;


static const char *fpmi_state_names[] = {
	[FPMI_PCTL_STATE_NORMAL] = "normal",
	[FPMI_PCTL_STATE_RELOADING] = "reloading",
	[FPMI_PCTL_STATE_TERMINATING] = "terminating",
	[FPMI_PCTL_STATE_FINISHING] = "finishing"
};

static int saved_argc;
static char **saved_argv;

static void fpmi_pctl_cleanup(int which, void *arg) /* {{{ */
{
	int i;
	if (which != FPMI_CLEANUP_PARENT_EXEC) {
		for (i = 0; i < saved_argc; i++) {
			free(saved_argv[i]);
		}
		free(saved_argv);
	}
}
/* }}} */

static struct fpmi_event_s pctl_event;

static void fpmi_pctl_action(struct fpmi_event_s *ev, short which, void *arg) /* {{{ */
{
	fpmi_pctl(FPMI_PCTL_STATE_UNSPECIFIED, FPMI_PCTL_ACTION_TIMEOUT);
}
/* }}} */

static int fpmi_pctl_timeout_set(int sec) /* {{{ */
{
	fpmi_event_set_timer(&pctl_event, 0, &fpmi_pctl_action, NULL);
	fpmi_event_add(&pctl_event, sec * 1000);
	return 0;
}
/* }}} */

static void fpmi_pctl_exit() /* {{{ */
{
	zlog(ZLOG_NOTICE, "exiting, bye-bye!");

	fpmi_conf_unlink_pid();
	fpmi_cleanups_run(FPMI_CLEANUP_PARENT_EXIT_MAIN);
	exit(FPMI_EXIT_OK);
}
/* }}} */

#define optional_arg(c) (saved_argc > c ? ", \"" : ""), (saved_argc > c ? saved_argv[c] : ""), (saved_argc > c ? "\"" : "")

static void fpmi_pctl_exec() /* {{{ */
{
	zlog(ZLOG_DEBUG, "Blocking some signals before reexec");
	if (0 > fpmi_signals_block()) {
		zlog(ZLOG_WARNING, "concurrent reloads may be unstable");
	}

	zlog(ZLOG_NOTICE, "reloading: execvp(\"%s\", {\"%s\""
			"%s%s%s" "%s%s%s" "%s%s%s" "%s%s%s" "%s%s%s"
			"%s%s%s" "%s%s%s" "%s%s%s" "%s%s%s" "%s%s%s"
		"})",
		saved_argv[0], saved_argv[0],
		optional_arg(1),
		optional_arg(2),
		optional_arg(3),
		optional_arg(4),
		optional_arg(5),
		optional_arg(6),
		optional_arg(7),
		optional_arg(8),
		optional_arg(9),
		optional_arg(10)
	);

	fpmi_cleanups_run(FPMI_CLEANUP_PARENT_EXEC);
	execvp(saved_argv[0], saved_argv);
	zlog(ZLOG_SYSERROR, "failed to reload: execvp() failed");
	exit(FPMI_EXIT_SOFTWARE);
}
/* }}} */

static void fpmi_pctl_action_last() /* {{{ */
{
	switch (fpmi_state) {
		case FPMI_PCTL_STATE_RELOADING:
			fpmi_pctl_exec();
			break;

		case FPMI_PCTL_STATE_FINISHING:
		case FPMI_PCTL_STATE_TERMINATING:
			fpmi_pctl_exit();
			break;
	}
}
/* }}} */

int fpmi_pctl_kill(pid_t pid, int how) /* {{{ */
{
	int s = 0;

	switch (how) {
		case FPMI_PCTL_TERM :
			s = SIGTERM;
			break;
		case FPMI_PCTL_STOP :
			s = SIGSTOP;
			break;
		case FPMI_PCTL_CONT :
			s = SIGCONT;
			break;
		case FPMI_PCTL_QUIT :
			s = SIGQUIT;
			break;
		default :
			break;
	}
	return kill(pid, s);
}
/* }}} */

void fpmi_pctl_kill_all(int signo) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;
	int alive_children = 0;

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		struct fpmi_child_s *child;

		for (child = wp->children; child; child = child->next) {
			int res = kill(child->pid, signo);

			zlog(ZLOG_DEBUG, "[pool %s] sending signal %d %s to child %d",
				child->wp->config->name, signo,
				fpmi_signal_names[signo] ? fpmi_signal_names[signo] : "", (int) child->pid);

			if (res == 0) {
				++alive_children;
			}
		}
	}

	if (alive_children) {
		zlog(ZLOG_DEBUG, "%d child(ren) still alive", alive_children);
	}
}
/* }}} */

static void fpmi_pctl_action_next() /* {{{ */
{
	int sig, timeout;

	if (!fpmi_globals.running_children) {
		fpmi_pctl_action_last();
	}

	if (fpmi_signal_sent == 0) {
		if (fpmi_state == FPMI_PCTL_STATE_TERMINATING) {
			sig = SIGTERM;
		} else {
			sig = SIGQUIT;
		}
		timeout = fpmi_global_config.process_control_timeout;
	} else {
		if (fpmi_signal_sent == SIGQUIT) {
			sig = SIGTERM;
		} else {
			sig = SIGKILL;
		}
		timeout = 1;
	}

	fpmi_pctl_kill_all(sig);
	fpmi_signal_sent = sig;
	fpmi_pctl_timeout_set(timeout);
}
/* }}} */

void fpmi_pctl(int new_state, int action) /* {{{ */
{
	switch (action) {
		case FPMI_PCTL_ACTION_SET :
			if (fpmi_state == new_state) { /* already in progress - just ignore duplicate signal */
				return;
			}

			switch (fpmi_state) { /* check which states can be overridden */
				case FPMI_PCTL_STATE_NORMAL :
					/* 'normal' can be overridden by any other state */
					break;
				case FPMI_PCTL_STATE_RELOADING :
					/* 'reloading' can be overridden by 'finishing' */
					if (new_state == FPMI_PCTL_STATE_FINISHING) break;
				case FPMI_PCTL_STATE_FINISHING :
					/* 'reloading' and 'finishing' can be overridden by 'terminating' */
					if (new_state == FPMI_PCTL_STATE_TERMINATING) break;
				case FPMI_PCTL_STATE_TERMINATING :
					/* nothing can override 'terminating' state */
					zlog(ZLOG_DEBUG, "not switching to '%s' state, because already in '%s' state",
						fpmi_state_names[new_state], fpmi_state_names[fpmi_state]);
					return;
			}

			fpmi_signal_sent = 0;
			fpmi_state = new_state;

			zlog(ZLOG_DEBUG, "switching to '%s' state", fpmi_state_names[fpmi_state]);
			/* fall down */

		case FPMI_PCTL_ACTION_TIMEOUT :
			fpmi_pctl_action_next();
			break;
		case FPMI_PCTL_ACTION_LAST_CHILD_EXITED :
			fpmi_pctl_action_last();
			break;

	}
}
/* }}} */

int fpmi_pctl_can_spawn_children() /* {{{ */
{
	return fpmi_state == FPMI_PCTL_STATE_NORMAL;
}
/* }}} */

int fpmi_pctl_child_exited() /* {{{ */
{
	if (fpmi_state == FPMI_PCTL_STATE_NORMAL) {
		return 0;
	}

	if (!fpmi_globals.running_children) {
		fpmi_pctl(FPMI_PCTL_STATE_UNSPECIFIED, FPMI_PCTL_ACTION_LAST_CHILD_EXITED);
	}
	return 0;
}
/* }}} */

int fpmi_pctl_init_main() /* {{{ */
{
	int i;

	saved_argc = fpmi_globals.argc;
	saved_argv = malloc(sizeof(char *) * (saved_argc + 1));

	if (!saved_argv) {
		return -1;
	}

	for (i = 0; i < saved_argc; i++) {
		saved_argv[i] = strdup(fpmi_globals.argv[i]);

		if (!saved_argv[i]) {
			return -1;
		}
	}

	saved_argv[i] = 0;

	if (0 > fpmi_cleanup_add(FPMI_CLEANUP_ALL, fpmi_pctl_cleanup, 0)) {
		return -1;
	}
	return 0;
}
/* }}} */

static void fpmi_pctl_check_request_timeout(struct timeval *now) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		int terminate_timeout = wp->config->request_terminate_timeout;
		int slowlog_timeout = wp->config->request_slowlog_timeout;
		struct fpmi_child_s *child;

		if (terminate_timeout || slowlog_timeout) {
			for (child = wp->children; child; child = child->next) {
				fpmi_request_check_timed_out(child, now, terminate_timeout, slowlog_timeout);
			}
		}
	}
}
/* }}} */

static void fpmi_pctl_perform_idle_server_maintenance(struct timeval *now) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		struct fpmi_child_s *child;
		struct fpmi_child_s *last_idle_child = NULL;
		int idle = 0;
		int active = 0;
		int children_to_fork;
		unsigned cur_lq = 0;

		if (wp->config == NULL) continue;

		for (child = wp->children; child; child = child->next) {
			if (fpmi_request_is_idle(child)) {
				if (last_idle_child == NULL) {
					last_idle_child = child;
				} else {
					if (timercmp(&child->started, &last_idle_child->started, <)) {
						last_idle_child = child;
					}
				}
				idle++;
			} else {
				active++;
			}
		}

		/* update status structure for all PMs */
		if (wp->listen_address_domain == FPMI_AF_INET) {
			if (0 > fpmi_socket_get_listening_queue(wp->listening_socket, &cur_lq, NULL)) {
				cur_lq = 0;
#if 0
			} else {
				if (cur_lq > 0) {
					if (!wp->warn_lq) {
						zlog(ZLOG_WARNING, "[pool %s] listening queue is not empty, #%d requests are waiting to be served, consider raising pm.max_children setting (%d)", wp->config->name, cur_lq, wp->config->pm_max_children);
						wp->warn_lq = 1;
					}
				} else {
					wp->warn_lq = 0;
				}
#endif
			}
		}
		fpmi_scoreboard_update(idle, active, cur_lq, -1, -1, -1, 0, FPMI_SCOREBOARD_ACTION_SET, wp->scoreboard);

		/* this is specific to PM_STYLE_ONDEMAND */
		if (wp->config->pm == PM_STYLE_ONDEMAND) {
			struct timeval last, now;

			zlog(ZLOG_DEBUG, "[pool %s] currently %d active children, %d spare children", wp->config->name, active, idle);

			if (!last_idle_child) continue;

			fpmi_request_last_activity(last_idle_child, &last);
			fpmi_clock_get(&now);
			if (last.tv_sec < now.tv_sec - wp->config->pm_process_idle_timeout) {
				last_idle_child->idle_kill = 1;
				fpmi_pctl_kill(last_idle_child->pid, FPMI_PCTL_QUIT);
			}

			continue;
		}

		/* the rest is only used by PM_STYLE_DYNAMIC */
		if (wp->config->pm != PM_STYLE_DYNAMIC) continue;

		zlog(ZLOG_DEBUG, "[pool %s] currently %d active children, %d spare children, %d running children. Spawning rate %d", wp->config->name, active, idle, wp->running_children, wp->idle_spawn_rate);

		if (idle > wp->config->pm_max_spare_servers && last_idle_child) {
			last_idle_child->idle_kill = 1;
			fpmi_pctl_kill(last_idle_child->pid, FPMI_PCTL_QUIT);
			wp->idle_spawn_rate = 1;
			continue;
		}

		if (idle < wp->config->pm_min_spare_servers) {
			if (wp->running_children >= wp->config->pm_max_children) {
				if (!wp->warn_max_children) {
					fpmi_scoreboard_update(0, 0, 0, 0, 0, 1, 0, FPMI_SCOREBOARD_ACTION_INC, wp->scoreboard);
					zlog(ZLOG_WARNING, "[pool %s] server reached pm.max_children setting (%d), consider raising it", wp->config->name, wp->config->pm_max_children);
					wp->warn_max_children = 1;
				}
				wp->idle_spawn_rate = 1;
				continue;
			}

			if (wp->idle_spawn_rate >= 8) {
				zlog(ZLOG_WARNING, "[pool %s] seems busy (you may need to increase pm.start_servers, or pm.min/max_spare_servers), spawning %d children, there are %d idle, and %d total children", wp->config->name, wp->idle_spawn_rate, idle, wp->running_children);
			}

			/* compute the number of idle process to spawn */
			children_to_fork = MIN(wp->idle_spawn_rate, wp->config->pm_min_spare_servers - idle);

			/* get sure it won't exceed max_children */
			children_to_fork = MIN(children_to_fork, wp->config->pm_max_children - wp->running_children);
			if (children_to_fork <= 0) {
				if (!wp->warn_max_children) {
					fpmi_scoreboard_update(0, 0, 0, 0, 0, 1, 0, FPMI_SCOREBOARD_ACTION_INC, wp->scoreboard);
					zlog(ZLOG_WARNING, "[pool %s] server reached pm.max_children setting (%d), consider raising it", wp->config->name, wp->config->pm_max_children);
					wp->warn_max_children = 1;
				}
				wp->idle_spawn_rate = 1;
				continue;
			}
			wp->warn_max_children = 0;

			fpmi_children_make(wp, 1, children_to_fork, 1);

			/* if it's a child, stop here without creating the next event
			 * this event is reserved to the master process
			 */
			if (fpmi_globals.is_child) {
				return;
			}

			zlog(ZLOG_DEBUG, "[pool %s] %d child(ren) have been created dynamically", wp->config->name, children_to_fork);

			/* Double the spawn rate for the next iteration */
			if (wp->idle_spawn_rate < FPMI_MAX_SPAWN_RATE) {
				wp->idle_spawn_rate *= 2;
			}
			continue;
		}
		wp->idle_spawn_rate = 1;
	}
}
/* }}} */

void fpmi_pctl_heartbeat(struct fpmi_event_s *ev, short which, void *arg) /* {{{ */
{
	static struct fpmi_event_s heartbeat;
	struct timeval now;

	if (fpmi_globals.parent_pid != getpid()) {
		return; /* sanity check */
	}

	if (which == FPMI_EV_TIMEOUT) {
		fpmi_clock_get(&now);
		fpmi_pctl_check_request_timeout(&now);
		return;
	}

	/* ensure heartbeat is not lower than FPMI_PCTL_MIN_HEARTBEAT */
	fpmi_globals.heartbeat = MAX(fpmi_globals.heartbeat, FPMI_PCTL_MIN_HEARTBEAT);

	/* first call without setting to initialize the timer */
	zlog(ZLOG_DEBUG, "heartbeat have been set up with a timeout of %dms", fpmi_globals.heartbeat);
	fpmi_event_set_timer(&heartbeat, FPMI_EV_PERSIST, &fpmi_pctl_heartbeat, NULL);
	fpmi_event_add(&heartbeat, fpmi_globals.heartbeat);
}
/* }}} */

void fpmi_pctl_perform_idle_server_maintenance_heartbeat(struct fpmi_event_s *ev, short which, void *arg) /* {{{ */
{
	static struct fpmi_event_s heartbeat;
	struct timeval now;

	if (fpmi_globals.parent_pid != getpid()) {
		return; /* sanity check */
	}

	if (which == FPMI_EV_TIMEOUT) {
		fpmi_clock_get(&now);
		if (fpmi_pctl_can_spawn_children()) {
			fpmi_pctl_perform_idle_server_maintenance(&now);

			/* if it's a child, stop here without creating the next event
			 * this event is reserved to the master process
			 */
			if (fpmi_globals.is_child) {
				return;
			}
		}
		return;
	}

	/* first call without setting which to initialize the timer */
	fpmi_event_set_timer(&heartbeat, FPMI_EV_PERSIST, &fpmi_pctl_perform_idle_server_maintenance_heartbeat, NULL);
	fpmi_event_add(&heartbeat, FPMI_IDLE_SERVER_MAINTENANCE_HEARTBEAT);
}
/* }}} */

void fpmi_pctl_on_socket_accept(struct fpmi_event_s *ev, short which, void *arg) /* {{{ */
{
	struct fpmi_worker_pool_s *wp = (struct fpmi_worker_pool_s *)arg;
	struct fpmi_child_s *child;


	if (fpmi_globals.parent_pid != getpid()) {
		/* prevent a event race condition when child process
		 * have not set up its own event loop */
		return;
	}

	wp->socket_event_set = 0;

/*	zlog(ZLOG_DEBUG, "[pool %s] heartbeat running_children=%d", wp->config->name, wp->running_children);*/

	if (wp->running_children >= wp->config->pm_max_children) {
		if (!wp->warn_max_children) {
			fpmi_scoreboard_update(0, 0, 0, 0, 0, 1, 0, FPMI_SCOREBOARD_ACTION_INC, wp->scoreboard);
			zlog(ZLOG_WARNING, "[pool %s] server reached max_children setting (%d), consider raising it", wp->config->name, wp->config->pm_max_children);
			wp->warn_max_children = 1;
		}

		return;
	}

	for (child = wp->children; child; child = child->next) {
		/* if there is at least on idle child, it will handle the connection, stop here */
		if (fpmi_request_is_idle(child)) {
			return;
		}
	}

	wp->warn_max_children = 0;
	fpmi_children_make(wp, 1, 1, 1);

	if (fpmi_globals.is_child) {
		return;
	}

	zlog(ZLOG_DEBUG, "[pool %s] got accept without idle child available .... I forked", wp->config->name);
}
/* }}} */

