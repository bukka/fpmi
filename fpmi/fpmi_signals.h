
	/* $Id: fpmi_signals.h,v 1.5 2008/05/24 17:38:47 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_SIGNALS_H
#define FPMI_SIGNALS_H 1

#include <signal.h>

int fpmi_signals_init_main();
int fpmi_signals_init_child();
int fpmi_signals_get_fd();

extern const char *fpmi_signal_names[NSIG + 1];

#endif
