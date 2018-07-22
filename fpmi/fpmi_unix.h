
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_UNIX_H
#define FPMI_UNIX_H 1

#include "fpmi_worker_pool.h"

int fpmi_unix_resolve_socket_premissions(struct fpmi_worker_pool_s *wp);
int fpmi_unix_set_socket_premissions(struct fpmi_worker_pool_s *wp, const char *path);
int fpmi_unix_free_socket_premissions(struct fpmi_worker_pool_s *wp);

int fpmi_unix_init_child(struct fpmi_worker_pool_s *wp);
int fpmi_unix_init_main();

extern size_t fpmi_pagesize;

#endif

