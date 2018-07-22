
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_CLEANUP_H
#define FPMI_CLEANUP_H 1

int fpmi_cleanup_add(int type, void (*cleanup)(int, void *), void *);
void fpmi_cleanups_run(int type);

enum {
	FPMI_CLEANUP_CHILD					= (1 << 0),
	FPMI_CLEANUP_PARENT_EXIT				= (1 << 1),
	FPMI_CLEANUP_PARENT_EXIT_MAIN		= (1 << 2),
	FPMI_CLEANUP_PARENT_EXEC				= (1 << 3),
	FPMI_CLEANUP_PARENT					= (1 << 1) | (1 << 2) | (1 << 3),
	FPMI_CLEANUP_ALL						= ~0,
};

#endif

