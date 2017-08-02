
	/* $Id: fpmi_stdio.h,v 1.9 2008/05/24 17:38:47 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_STDIO_H
#define FPMI_STDIO_H 1

#include "fpmi_worker_pool.h"

int fpmi_stdio_init_main();
int fpmi_stdio_init_final();
int fpmi_stdio_init_child(struct fpmi_worker_pool_s *wp);
int fpmi_stdio_prepare_pipes(struct fpmi_child_s *child);
void fpmi_stdio_child_use_pipes(struct fpmi_child_s *child);
int fpmi_stdio_parent_use_pipes(struct fpmi_child_s *child);
int fpmi_stdio_discard_pipes(struct fpmi_child_s *child);
int fpmi_stdio_open_error_log(int reopen);

#endif

