
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_SHM_H
#define FPMI_SHM_H 1

void *fpmi_shm_alloc(size_t size);
int fpmi_shm_free(void *mem, size_t size);
size_t fpmi_shm_get_size_allocated();

#endif

