
	/* (c) 2009 Jerome Loyet */

#ifndef FPMI_LOG_H
#define FPMI_LOG_H 1
#include "fpmi_worker_pool.h"

int fpmi_log_init_child(struct fpmi_worker_pool_s *wp);
int fpmi_log_write(char *log_format);
int fpmi_log_open(int reopen);

#endif
