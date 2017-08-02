
	/* $Id: fpmi_trace_mach.c,v 1.4 2008/08/26 15:09:15 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <unistd.h>

#include "fpmi_trace.h"
#include "fpmi_process_ctl.h"
#include "fpmi_unix.h"
#include "zlog.h"


static mach_port_name_t target;
static vm_offset_t target_page_base;
static vm_offset_t local_page;
static mach_msg_type_number_t local_size;

static void fpmi_mach_vm_deallocate() /* {{{ */
{
	if (local_page) {
		mach_vm_deallocate(mach_task_self(), local_page, local_size);
		target_page_base = 0;
		local_page = 0;
		local_size = 0;
	}
}
/* }}} */

static int fpmi_mach_vm_read_page(vm_offset_t page) /* {{{ */
{
	kern_return_t kr;

	kr = mach_vm_read(target, page, fpmi_pagesize, &local_page, &local_size);
	if (kr != KERN_SUCCESS) {
		zlog(ZLOG_ERROR, "failed to read vm page: mach_vm_read(): %s (%d)", mach_error_string(kr), kr);
		return -1;
	}
	return 0;
}
/* }}} */

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
	kern_return_t kr;

	kr = task_for_pid(mach_task_self(), pid, &target);
	if (kr != KERN_SUCCESS) {
		char *msg = "";

		if (kr == KERN_FAILURE) {
			msg = " It seems that master process does not have enough privileges to trace processes.";
		}
		zlog(ZLOG_ERROR, "task_for_pid() failed: %s (%d)%s", mach_error_string(kr), kr, msg);
		return -1;
	}
	return 0;
}
/* }}} */

int fpmi_trace_close(pid_t pid) /* {{{ */
{
	fpmi_mach_vm_deallocate();
	target = 0;
	return 0;
}
/* }}} */

int fpmi_trace_get_long(long addr, long *data) /* {{{ */
{
	size_t offset = ((uintptr_t) (addr) % fpmi_pagesize);
	vm_offset_t base = (uintptr_t) (addr) - offset;

	if (base != target_page_base) {
		fpmi_mach_vm_deallocate();
		if (0 > fpmi_mach_vm_read_page(base)) {
			return -1;
		}
	}
	*data = * (long *) (local_page + offset);
	return 0;
}
/* }}} */


