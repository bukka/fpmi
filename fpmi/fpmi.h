
	/* $Id: fpmi.h,v 1.13 2008/05/24 17:38:47 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_H
#define FPMI_H 1

#include <unistd.h>

#ifdef HAVE_SYSEXITS_H
#include <sysexits.h>
#endif

#ifdef EX_OK
#define FPMI_EXIT_OK EX_OK
#else
#define FPMI_EXIT_OK 0
#endif

#ifdef EX_USAGE
#define FPMI_EXIT_USAGE EX_USAGE
#else
#define FPMI_EXIT_USAGE 64
#endif

#ifdef EX_SOFTWARE
#define FPMI_EXIT_SOFTWARE EX_SOFTWARE
#else
#define FPMI_EXIT_SOFTWARE 70
#endif

#ifdef EX_CONFIG
#define FPMI_EXIT_CONFIG EX_CONFIG
#else
#define FPMI_EXIT_CONFIG 78
#endif


int fpmi_run(int *max_requests);
int fpmi_init(int argc, char **argv, char *config, char *prefix, char *pid, int test_conf, int run_as_root, int force_daemon, int force_stderr);

struct fpmi_globals_s {
	pid_t parent_pid;
	int argc;
	char **argv;
	char *config;
	char *prefix;
	char *pid;
	int running_children;
	int error_log_fd;
	int log_level;
	int listening_socket; /* for this child */
	int max_requests; /* for this child */
	int is_child;
	int test_successful;
	int heartbeat;
	int run_as_root;
	int force_stderr;
	int send_config_pipe[2];
	int workers_output_limit;
};

extern struct fpmi_globals_s fpmi_globals;

#endif
