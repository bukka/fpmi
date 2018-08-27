
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_CHILDREN_H
#define FPMI_CHILDREN_H 1

#include <sys/time.h>
#include <sys/types.h>

#include "fpmi_worker_pool.h"
#include "fpmi_events.h"
#include "zlog.h"

int fpmi_children_create_initial(struct fpmi_worker_pool_s *wp);
int fpmi_children_free(struct fpmi_child_s *child);
void fpmi_children_bury();
int fpmi_children_init_main();
int fpmi_children_make(struct fpmi_worker_pool_s *wp, int in_event_loop, int nb_to_spawn, int is_debug);

struct fpmi_child_s;

struct fpmi_child_s {
	struct fpmi_child_s *prev, *next;
	struct timeval started;
	struct fpmi_worker_pool_s *wp;
	struct fpmi_event_s ev_stdout, ev_stderr;
	int shm_slot_i;
	int fd_stdout, fd_stderr;
	void (*tracer)(struct fpmi_child_s *);
	struct timeval slow_logged;
	int idle_kill;
	pid_t pid;
	int scoreboard_i;
	struct zlog_stream *log_stream;
};

#endif
