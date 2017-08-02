
	/* $Id: fpmi_worker_pool.h,v 1.13 2008/08/26 15:09:15 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_WORKER_POOL_H
#define FPMI_WORKER_POOL_H 1

#include "fpmi_conf.h"
#include "fpmi_shm.h"

struct fpmi_worker_pool_s;
struct fpmi_child_s;
struct fpmi_child_stat_s;
struct fpmi_shm_s;

enum fpmi_address_domain {
	FPMI_AF_UNIX = 1,
	FPMI_AF_INET = 2
};

struct fpmi_worker_pool_s {
	struct fpmi_worker_pool_s *next;
	struct fpmi_worker_pool_config_s *config;
	char *user, *home;									/* for setting env USER and HOME */
	enum fpmi_address_domain listen_address_domain;
	int listening_socket;
	int set_uid, set_gid;								/* config uid and gid */
	int socket_uid, socket_gid, socket_mode;

	/* runtime */
	struct fpmi_child_s *children;
	int running_children;
	int idle_spawn_rate;
	int warn_max_children;
#if 0
	int warn_lq;
#endif
	struct fpmi_scoreboard_s *scoreboard;
	int log_fd;
	char **limit_extensions;

	/* for ondemand PM */
	struct fpmi_event_s *ondemand_event;
	int socket_event_set;

#ifdef HAVE_FPMI_ACL
	void *socket_acl;
#endif
};

struct fpmi_worker_pool_s *fpmi_worker_pool_alloc();
void fpmi_worker_pool_free(struct fpmi_worker_pool_s *wp);
int fpmi_worker_pool_init_main();

extern struct fpmi_worker_pool_s *fpmi_worker_all_pools;

#endif

