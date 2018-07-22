
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <stdlib.h> /* for exit */

#include "fpmi.h"
#include "fpmi_children.h"
#include "fpmi_signals.h"
#include "fpmi_env.h"
#include "fpmi_events.h"
#include "fpmi_cleanup.h"
#include "fpmi_php.h"
#include "fpmi_sockets.h"
#include "fpmi_unix.h"
#include "fpmi_process_ctl.h"
#include "fpmi_conf.h"
#include "fpmi_worker_pool.h"
#include "fpmi_scoreboard.h"
#include "fpmi_stdio.h"
#include "fpmi_log.h"
#include "zlog.h"

struct fpmi_globals_s fpmi_globals = {
	.parent_pid = 0,
	.argc = 0,
	.argv = NULL,
	.config = NULL,
	.prefix = NULL,
	.pid = NULL,
	.running_children = 0,
	.error_log_fd = 0,
	.log_level = 0,
	.listening_socket = 0,
	.max_requests = 0,
	.is_child = 0,
	.test_successful = 0,
	.heartbeat = 0,
	.run_as_root = 0,
	.force_stderr = 0,
	.send_config_pipe = {0, 0}
};

int fpmi_init(int argc, char **argv, char *config, char *prefix, char *pid, int test_conf, int run_as_root, int force_daemon, int force_stderr) /* {{{ */
{
	fpmi_globals.argc = argc;
	fpmi_globals.argv = argv;
	if (config && *config) {
		fpmi_globals.config = strdup(config);
	}
	fpmi_globals.prefix = prefix;
	fpmi_globals.pid = pid;
	fpmi_globals.run_as_root = run_as_root;
	fpmi_globals.force_stderr = force_stderr;

	if (0 > fpmi_php_init_main()           ||
	    0 > fpmi_stdio_init_main()         ||
	    0 > fpmi_conf_init_main(test_conf, force_daemon) ||
	    0 > fpmi_unix_init_main()          ||
	    0 > fpmi_scoreboard_init_main()    ||
	    0 > fpmi_pctl_init_main()          ||
	    0 > fpmi_env_init_main()           ||
	    0 > fpmi_signals_init_main()       ||
	    0 > fpmi_children_init_main()      ||
	    0 > fpmi_sockets_init_main()       ||
	    0 > fpmi_worker_pool_init_main()   ||
	    0 > fpmi_event_init_main()) {

		if (fpmi_globals.test_successful) {
			exit(FPMI_EXIT_OK);
		} else {
			zlog(ZLOG_ERROR, "FPMI initialization failed");
			return -1;
		}
	}

	if (0 > fpmi_conf_write_pid()) {
		zlog(ZLOG_ERROR, "FPMI initialization failed");
		return -1;
	}

	fpmi_stdio_init_final();
	zlog(ZLOG_NOTICE, "fpmi is running, pid %d", (int) fpmi_globals.parent_pid);

	return 0;
}
/* }}} */

/*	children: return listening socket
	parent: never return */
int fpmi_run(int *max_requests) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;

	/* create initial children in all pools */
	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		int is_parent;

		is_parent = fpmi_children_create_initial(wp);

		if (!is_parent) {
			goto run_child;
		}

		/* handle error */
		if (is_parent == 2) {
			fpmi_pctl(FPMI_PCTL_STATE_TERMINATING, FPMI_PCTL_ACTION_SET);
			fpmi_event_loop(1);
		}
	}

	/* run event loop forever */
	fpmi_event_loop(0);

run_child: /* only workers reach this point */

	fpmi_cleanups_run(FPMI_CLEANUP_CHILD);

	*max_requests = fpmi_globals.max_requests;
	return fpmi_globals.listening_socket;
}
/* }}} */

