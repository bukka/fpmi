
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_ENV_H
#define FPMI_ENV_H 1

#include "fpmi_worker_pool.h"

#define SETPROCTITLE_PREFIX "php-fpmi: "

int fpmi_env_init_child(struct fpmi_worker_pool_s *wp);
int fpmi_env_init_main();
void fpmi_env_setproctitle(char *title);

extern char **environ;

#ifndef HAVE_SETENV
int setenv(char *name, char *value, int overwrite);
#endif

#ifndef HAVE_CLEARENV
void clearenv();
#endif

#endif

