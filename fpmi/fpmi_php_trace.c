
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#if HAVE_FPMI_TRACE

#include "php.h"
#include "php_main.h"

#include <stdio.h>
#include <stddef.h>
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# include <stdint.h>
#endif
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>

#include "fpmi_trace.h"
#include "fpmi_php_trace.h"
#include "fpmi_children.h"
#include "fpmi_worker_pool.h"
#include "fpmi_process_ctl.h"
#include "fpmi_scoreboard.h"

#include "zlog.h"


#define valid_ptr(p) ((p) && 0 == ((p) & (sizeof(long) - 1)))

#if SIZEOF_LONG == 4
#define PTR_FMT "08"
#elif SIZEOF_LONG == 8
#define PTR_FMT "016"
#endif

#if PHP_VERSION_ID < 70399
#define FPMI_CALL_IS_TOP_CODE(_call_info) \
	(ZEND_CALL_KIND_EX((*_call_info) >> ZEND_CALL_INFO_SHIFT) == ZEND_CALL_TOP_CODE)
#define FPMI_CALL_IS_NESTED_CODE(_call_info) \
	(ZEND_CALL_KIND_EX(*(_call_info) >> ZEND_CALL_INFO_SHIFT) == ZEND_CALL_NESTED_CODE)
#else
#define FPMI_CALL_IS_TOP_CODE(_call_info) \
	(ZEND_CALL_KIND_EX(*_call_info) == ZEND_CALL_TOP_CODE)
#define FPMI_CALL_IS_NESTED_CODE(_call_info) \
	(ZEND_CALL_KIND_EX(*_call_info) == ZEND_CALL_NESTED_CODE)
#endif

static int fpmi_php_trace_dump(struct fpmi_child_s *child, FILE *slowlog) /* {{{ */
{
	int callers_limit = child->wp->config->request_slowlog_trace_depth;
	pid_t pid = child->pid;
	struct timeval tv;
	static const int buf_size = 1024;
	char buf[buf_size];
	long execute_data;
	long path_translated;
	long l;

	gettimeofday(&tv, 0);

	zlog_print_time(&tv, buf, buf_size);

	fprintf(slowlog, "\n%s [pool %s] pid %d\n", buf, child->wp->config->name, (int) pid);

	if (0 > fpmi_trace_get_long((long) &SG(request_info).path_translated, &l)) {
		return -1;
	}

	path_translated = l;

	if (0 > fpmi_trace_get_strz(buf, buf_size, path_translated)) {
		return -1;
	}

	fprintf(slowlog, "script_filename = %s\n", buf);

	if (0 > fpmi_trace_get_long((long) &EG(current_execute_data), &l)) {
		return -1;
	}

	execute_data = l;

	while (execute_data) {
		long function;
		long function_name;
		long file_name;
		long prev;
		uint32_t lineno = 0;

		if (0 > fpmi_trace_get_long(execute_data + offsetof(zend_execute_data, func), &l)) {
			return -1;
		}

		function = l;

		if (valid_ptr(function)) {
			if (0 > fpmi_trace_get_long(function + offsetof(zend_function, common.function_name), &l)) {
				return -1;
			}

			function_name = l;

			if (function_name == 0) {
				uint32_t *call_info = (uint32_t *)&l;
				if (0 > fpmi_trace_get_long(execute_data + offsetof(zend_execute_data, This.u1.type_info), &l)) {
					return -1;
				}

				if (FPMI_CALL_IS_TOP_CODE(call_info)) {
					return 0;
				} else if (FPMI_CALL_IS_NESTED_CODE(call_info)) {
					memcpy(buf, "[INCLUDE_OR_EVAL]", sizeof("[INCLUDE_OR_EVAL]"));
				} else {
					ZEND_ASSERT(0);
				}
			} else {
				if (0 > fpmi_trace_get_strz(buf, buf_size, function_name + offsetof(zend_string, val))) {
					return -1;
				}

			}
		} else {
			memcpy(buf, "???", sizeof("???"));
		}
		
		fprintf(slowlog, "[0x%" PTR_FMT "lx] ", execute_data);

		fprintf(slowlog, "%s()", buf);

		*buf = '\0';

		if (0 > fpmi_trace_get_long(execute_data + offsetof(zend_execute_data, prev_execute_data), &l)) {
			return -1;
		}

		execute_data = prev = l;

		while (prev) {
			zend_uchar *type;

			if (0 > fpmi_trace_get_long(prev + offsetof(zend_execute_data, func), &l)) {
				return -1;
			}

			function = l;

			if (!valid_ptr(function)) {
				break;
			}

			type = (zend_uchar *)&l;
			if (0 > fpmi_trace_get_long(function + offsetof(zend_function, type), &l)) {
				return -1;
			}

			if (ZEND_USER_CODE(*type)) {
				if (0 > fpmi_trace_get_long(function + offsetof(zend_op_array, filename), &l)) {
					return -1;
				}

				file_name = l;

				if (0 > fpmi_trace_get_strz(buf, buf_size, file_name + offsetof(zend_string, val))) {
					return -1;
				}

				if (0 > fpmi_trace_get_long(prev + offsetof(zend_execute_data, opline), &l)) {
					return -1;
				}

				if (valid_ptr(l)) {
					long opline = l;
					uint32_t *lu = (uint32_t *) &l;

					if (0 > fpmi_trace_get_long(opline + offsetof(struct _zend_op, lineno), &l)) {
						return -1;
					}

					lineno = *lu;
				}
				break;
			}

			if (0 > fpmi_trace_get_long(prev + offsetof(zend_execute_data, prev_execute_data), &l)) {
				return -1;
			}

			prev = l;
		}

		fprintf(slowlog, " %s:%u\n", *buf ? buf : "unknown", lineno);

		if (0 == --callers_limit) {
			break;
		}
	}

	return 0;
}
/* }}} */

void fpmi_php_trace(struct fpmi_child_s *child) /* {{{ */
{
	fpmi_scoreboard_update(0, 0, 0, 0, 0, 0, 1, FPMI_SCOREBOARD_ACTION_INC, child->wp->scoreboard);
	FILE *slowlog;

	zlog(ZLOG_NOTICE, "about to trace %d", (int) child->pid);

	slowlog = fopen(child->wp->config->slowlog, "a+");

	if (!slowlog) {
		zlog(ZLOG_SYSERROR, "unable to open slowlog (%s)", child->wp->config->slowlog);
		goto done0;
	}

	if (0 > fpmi_trace_ready(child->pid)) {
		goto done1;
	}

	if (0 > fpmi_php_trace_dump(child, slowlog)) {
		fprintf(slowlog, "+++ dump failed\n");
	}

	if (0 > fpmi_trace_close(child->pid)) {
		goto done1;
	}

done1:
	fclose(slowlog);

done0:
	fpmi_pctl_kill(child->pid, FPMI_PCTL_CONT);
	child->tracer = 0;

	zlog(ZLOG_NOTICE, "finished trace of %d", (int) child->pid);
}
/* }}} */

#endif

