
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_SIGNALS_H
#define FPMI_SIGNALS_H 1

#include <signal.h>

int fpmi_signals_init_main();
int fpmi_signals_init_child();
int fpmi_signals_get_fd();
int fpmi_signals_init_mask();
int fpmi_signals_block();
int fpmi_signals_child_block();
int fpmi_signals_unblock();

extern const char *fpmi_signal_names[NSIG + 1];

#endif
