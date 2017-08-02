
	/* $Id: fpmi_trace_pread.c,v 1.7 2008/08/26 15:09:15 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "fpmi_config.h"

#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# include <stdint.h>
#endif

#include "fpmi_trace.h"
#include "fpmi_process_ctl.h"
#include "zlog.h"

static int mem_file = -1;

int fpmi_trace_signal(pid_t pid) /* {{{ */
{
	if (0 > fpmi_pctl_kill(pid, FPMI_PCTL_STOP)) {
		zlog(ZLOG_SYSERROR, "failed to send SIGSTOP to %d", pid);
		return -1;
	}
	return 0;
}
/* }}} */

int fpmi_trace_ready(pid_t pid) /* {{{ */
{
	char buf[128];

	sprintf(buf, "/proc/%d/" PROC_MEM_FILE, (int) pid);
	mem_file = open(buf, O_RDONLY);
	if (0 > mem_file) {
		zlog(ZLOG_SYSERROR, "failed to open %s", buf);
		return -1;
	}
	return 0;
}
/* }}} */

int fpmi_trace_close(pid_t pid) /* {{{ */
{
	close(mem_file);
	mem_file = -1;
	return 0;
}
/* }}} */

int fpmi_trace_get_long(long addr, long *data) /* {{{ */
{
	if (sizeof(*data) != pread(mem_file, (void *) data, sizeof(*data), (uintptr_t) addr)) {
		zlog(ZLOG_SYSERROR, "pread() failed");
		return -1;
	}
	return 0;
}
/* }}} */

