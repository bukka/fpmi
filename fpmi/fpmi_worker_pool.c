
	/* $Id: fpmi_worker_pool.c,v 1.15.2.1 2008/12/13 03:21:18 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "fpmi.h"
#include "fpmi_worker_pool.h"
#include "fpmi_cleanup.h"
#include "fpmi_children.h"
#include "fpmi_shm.h"
#include "fpmi_scoreboard.h"
#include "fpmi_conf.h"
#include "fpmi_unix.h"

struct fpmi_worker_pool_s *fpmi_worker_all_pools;

void fpmi_worker_pool_free(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	if (wp->config) {
		free(wp->config);
	}
	if (wp->user) {
		free(wp->user);
	}
	if (wp->home) {
		free(wp->home);
	}
	fpmi_unix_free_socket_premissions(wp);
	free(wp);
}
/* }}} */

static void fpmi_worker_pool_cleanup(int which, void *arg) /* {{{ */
{
	struct fpmi_worker_pool_s *wp, *wp_next;

	for (wp = fpmi_worker_all_pools; wp; wp = wp_next) {
		wp_next = wp->next;
		fpmi_worker_pool_config_free(wp->config);
		fpmi_children_free(wp->children);
		if ((which & FPMI_CLEANUP_CHILD) == 0 && fpmi_globals.parent_pid == getpid()) {
			fpmi_scoreboard_free(wp->scoreboard);
		}
		fpmi_worker_pool_free(wp);
	}
	fpmi_worker_all_pools = NULL;
}
/* }}} */

struct fpmi_worker_pool_s *fpmi_worker_pool_alloc() /* {{{ */
{
	struct fpmi_worker_pool_s *ret;

	ret = malloc(sizeof(struct fpmi_worker_pool_s));
	if (!ret) {
		return 0;
	}

	memset(ret, 0, sizeof(struct fpmi_worker_pool_s));

	ret->idle_spawn_rate = 1;
	ret->log_fd = -1;
	return ret;
}
/* }}} */

int fpmi_worker_pool_init_main() /* {{{ */
{
	if (0 > fpmi_cleanup_add(FPMI_CLEANUP_ALL, fpmi_worker_pool_cleanup, 0)) {
		return -1;
	}
	return 0;
}
/* }}} */
