
	/* (c) 2009 Jerome Loyet */

#ifndef FPMI_SCOREBOARD_H
#define FPMI_SCOREBOARD_H 1

#include <sys/time.h>
#ifdef HAVE_TIMES
#include <sys/times.h>
#endif

#include "fpmi_request.h"
#include "fpmi_worker_pool.h"
#include "fpmi_atomic.h"

#define FPMI_SCOREBOARD_ACTION_SET 0
#define FPMI_SCOREBOARD_ACTION_INC 1

struct fpmi_scoreboard_proc_s {
	union {
		atomic_t lock;
		char dummy[16];
	};
	int used;
	time_t start_epoch;
	pid_t pid;
	unsigned long requests;
	enum fpmi_request_stage_e request_stage;
	struct timeval accepted;
	struct timeval duration;
	time_t accepted_epoch;
	struct timeval tv;
	char request_uri[128];
	char query_string[512];
	char request_method[16];
	size_t content_length; /* used with POST only */
	char script_filename[256];
	char auth_user[32];
#ifdef HAVE_TIMES
	struct tms cpu_accepted;
	struct timeval cpu_duration;
	struct tms last_request_cpu;
	struct timeval last_request_cpu_duration;
#endif
	size_t memory;
};

struct fpmi_scoreboard_s {
	union {
		atomic_t lock;
		char dummy[16];
	};
	char pool[32];
	int pm;
	time_t start_epoch;
	int idle;
	int active;
	int active_max;
	unsigned long int requests;
	unsigned int max_children_reached;
	int lq;
	int lq_max;
	unsigned int lq_len;
	unsigned int nprocs;
	int free_proc;
	unsigned long int slow_rq;
	struct fpmi_scoreboard_proc_s *procs[];
};

int fpmi_scoreboard_init_main();
int fpmi_scoreboard_init_child(struct fpmi_worker_pool_s *wp);

void fpmi_scoreboard_update(int idle, int active, int lq, int lq_len, int requests, int max_children_reached, int slow_rq, int action, struct fpmi_scoreboard_s *scoreboard);
struct fpmi_scoreboard_s *fpmi_scoreboard_get();
struct fpmi_scoreboard_proc_s *fpmi_scoreboard_proc_get(struct fpmi_scoreboard_s *scoreboard, int child_index);

struct fpmi_scoreboard_s *fpmi_scoreboard_acquire(struct fpmi_scoreboard_s *scoreboard, int nohang);
void fpmi_scoreboard_release(struct fpmi_scoreboard_s *scoreboard);
struct fpmi_scoreboard_proc_s *fpmi_scoreboard_proc_acquire(struct fpmi_scoreboard_s *scoreboard, int child_index, int nohang);
void fpmi_scoreboard_proc_release(struct fpmi_scoreboard_proc_s *proc);

void fpmi_scoreboard_free(struct fpmi_scoreboard_s *scoreboard);

void fpmi_scoreboard_child_use(struct fpmi_scoreboard_s *scoreboard, int child_index, pid_t pid);

void fpmi_scoreboard_proc_free(struct fpmi_scoreboard_s *scoreboard, int child_index);
int fpmi_scoreboard_proc_alloc(struct fpmi_scoreboard_s *scoreboard, int *child_index);

#ifdef HAVE_TIMES
float fpmi_scoreboard_get_tick();
#endif

#endif
