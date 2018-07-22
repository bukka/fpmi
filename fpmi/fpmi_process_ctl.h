
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_PROCESS_CTL_H
#define FPMI_PROCESS_CTL_H 1

#include "fpmi_events.h"

/* spawn max 32 children at once */
#define FPMI_MAX_SPAWN_RATE (32)
/* 1s (in ms) heartbeat for idle server maintenance */
#define FPMI_IDLE_SERVER_MAINTENANCE_HEARTBEAT (1000)
/* a minimum of 130ms heartbeat for pctl */
#define FPMI_PCTL_MIN_HEARTBEAT (130)


struct fpmi_child_s;

void fpmi_pctl(int new_state, int action);
int fpmi_pctl_can_spawn_children();
int fpmi_pctl_kill(pid_t pid, int how);
void fpmi_pctl_kill_all(int signo);
void fpmi_pctl_heartbeat(struct fpmi_event_s *ev, short which, void *arg);
void fpmi_pctl_perform_idle_server_maintenance_heartbeat(struct fpmi_event_s *ev, short which, void *arg);
void fpmi_pctl_on_socket_accept(struct fpmi_event_s *ev, short which, void *arg);
int fpmi_pctl_child_exited();
int fpmi_pctl_init_main();


enum {
	FPMI_PCTL_STATE_UNSPECIFIED,
	FPMI_PCTL_STATE_NORMAL,
	FPMI_PCTL_STATE_RELOADING,
	FPMI_PCTL_STATE_TERMINATING,
	FPMI_PCTL_STATE_FINISHING
};

enum {
	FPMI_PCTL_ACTION_SET,
	FPMI_PCTL_ACTION_TIMEOUT,
	FPMI_PCTL_ACTION_LAST_CHILD_EXITED
};

enum {
	FPMI_PCTL_TERM,
	FPMI_PCTL_STOP,
	FPMI_PCTL_CONT,
	FPMI_PCTL_QUIT
};

#endif

