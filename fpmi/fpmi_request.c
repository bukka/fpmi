
	/* (c) 2007,2008 Andrei Nigmatulin */
#ifdef HAVE_TIMES
#include <sys/times.h>
#endif

#include "fpmi_config.h"

#include "fpmi.h"
#include "fpmi_php.h"
#include "fpmi_str.h"
#include "fpmi_clock.h"
#include "fpmi_conf.h"
#include "fpmi_trace.h"
#include "fpmi_php_trace.h"
#include "fpmi_process_ctl.h"
#include "fpmi_children.h"
#include "fpmi_scoreboard.h"
#include "fpmi_status.h"
#include "fpmi_stdio.h"
#include "fpmi_request.h"
#include "fpmi_log.h"

#include "zlog.h"

static const char *requests_stages[] = {
	[FPMI_REQUEST_ACCEPTING]       = "Idle",
	[FPMI_REQUEST_READING_HEADERS] = "Reading headers",
	[FPMI_REQUEST_INFO]            = "Getting request information",
	[FPMI_REQUEST_EXECUTING]       = "Running",
	[FPMI_REQUEST_END]             = "Ending",
	[FPMI_REQUEST_FINISHED]        = "Finishing",
};

const char *fpmi_request_get_stage_name(int stage) {
	return requests_stages[stage];
}

void fpmi_request_accepting() /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;
	struct timeval now;

	fpmi_clock_get(&now);

	proc = fpmi_scoreboard_proc_acquire(NULL, -1, 0);
	if (proc == NULL) {
		zlog(ZLOG_WARNING, "failed to acquire proc scoreboard");
		return;
	}

	proc->request_stage = FPMI_REQUEST_ACCEPTING;
	proc->tv = now;
	fpmi_scoreboard_proc_release(proc);

	/* idle++, active-- */
	fpmi_scoreboard_update(1, -1, 0, 0, 0, 0, 0, FPMI_SCOREBOARD_ACTION_INC, NULL);
}
/* }}} */

void fpmi_request_reading_headers() /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;

	struct timeval now;
	clock_t now_epoch;
#ifdef HAVE_TIMES
	struct tms cpu;
#endif

	fpmi_clock_get(&now);
	now_epoch = time(NULL);
#ifdef HAVE_TIMES
	times(&cpu);
#endif

	proc = fpmi_scoreboard_proc_acquire(NULL, -1, 0);
	if (proc == NULL) {
		zlog(ZLOG_WARNING, "failed to acquire proc scoreboard");
		return;
	}

	proc->request_stage = FPMI_REQUEST_READING_HEADERS;
	proc->tv = now;
	proc->accepted = now;
	proc->accepted_epoch = now_epoch;
#ifdef HAVE_TIMES
	proc->cpu_accepted = cpu;
#endif
	proc->requests++;
	proc->request_uri[0] = '\0';
	proc->request_method[0] = '\0';
	proc->script_filename[0] = '\0';
	proc->query_string[0] = '\0';
	proc->auth_user[0] = '\0';
	proc->content_length = 0;
	fpmi_scoreboard_proc_release(proc);

	/* idle--, active++, request++ */
	fpmi_scoreboard_update(-1, 1, 0, 0, 1, 0, 0, FPMI_SCOREBOARD_ACTION_INC, NULL);
}
/* }}} */

void fpmi_request_info() /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;
	char *request_uri = fpmi_php_request_uri();
	char *request_method = fpmi_php_request_method();
	char *script_filename = fpmi_php_script_filename();
	char *query_string = fpmi_php_query_string();
	char *auth_user = fpmi_php_auth_user();
	size_t content_length = fpmi_php_content_length();
	struct timeval now;

	fpmi_clock_get(&now);

	proc = fpmi_scoreboard_proc_acquire(NULL, -1, 0);
	if (proc == NULL) {
		zlog(ZLOG_WARNING, "failed to acquire proc scoreboard");
		return;
	}

	proc->request_stage = FPMI_REQUEST_INFO;
	proc->tv = now;

	if (request_uri) {
		strlcpy(proc->request_uri, request_uri, sizeof(proc->request_uri));
	}

	if (request_method) {
		strlcpy(proc->request_method, request_method, sizeof(proc->request_method));
	}

	if (query_string) {
		strlcpy(proc->query_string, query_string, sizeof(proc->query_string));
	}

	if (auth_user) {
		strlcpy(proc->auth_user, auth_user, sizeof(proc->auth_user));
	}

	proc->content_length = content_length;

	/* if cgi.fix_pathinfo is set to "1" and script cannot be found (404)
		the sapi_globals.request_info.path_translated is set to NULL */
	if (script_filename) {
		strlcpy(proc->script_filename, script_filename, sizeof(proc->script_filename));
	}

	fpmi_scoreboard_proc_release(proc);
}
/* }}} */

void fpmi_request_executing() /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;
	struct timeval now;

	fpmi_clock_get(&now);

	proc = fpmi_scoreboard_proc_acquire(NULL, -1, 0);
	if (proc == NULL) {
		zlog(ZLOG_WARNING, "failed to acquire proc scoreboard");
		return;
	}

	proc->request_stage = FPMI_REQUEST_EXECUTING;
	proc->tv = now;
	fpmi_scoreboard_proc_release(proc);
}
/* }}} */

void fpmi_request_end(void) /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;
	struct timeval now;
#ifdef HAVE_TIMES
	struct tms cpu;
#endif
	size_t memory = zend_memory_peak_usage(1);

	fpmi_clock_get(&now);
#ifdef HAVE_TIMES
	times(&cpu);
#endif

	proc = fpmi_scoreboard_proc_acquire(NULL, -1, 0);
	if (proc == NULL) {
		zlog(ZLOG_WARNING, "failed to acquire proc scoreboard");
		return;
	}
	proc->request_stage = FPMI_REQUEST_FINISHED;
	proc->tv = now;
	timersub(&now, &proc->accepted, &proc->duration);
#ifdef HAVE_TIMES
	timersub(&proc->tv, &proc->accepted, &proc->cpu_duration);
	proc->last_request_cpu.tms_utime = cpu.tms_utime - proc->cpu_accepted.tms_utime;
	proc->last_request_cpu.tms_stime = cpu.tms_stime - proc->cpu_accepted.tms_stime;
	proc->last_request_cpu.tms_cutime = cpu.tms_cutime - proc->cpu_accepted.tms_cutime;
	proc->last_request_cpu.tms_cstime = cpu.tms_cstime - proc->cpu_accepted.tms_cstime;
#endif
	proc->memory = memory;
	fpmi_scoreboard_proc_release(proc);
	fpmi_stdio_flush_child();
}
/* }}} */

void fpmi_request_finished() /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;
	struct timeval now;

	fpmi_clock_get(&now);

	proc = fpmi_scoreboard_proc_acquire(NULL, -1, 0);
	if (proc == NULL) {
		zlog(ZLOG_WARNING, "failed to acquire proc scoreboard");
		return;
	}

	proc->request_stage = FPMI_REQUEST_FINISHED;
	proc->tv = now;
	fpmi_scoreboard_proc_release(proc);
}
/* }}} */

void fpmi_request_check_timed_out(struct fpmi_child_s *child, struct timeval *now, int terminate_timeout, int slowlog_timeout) /* {{{ */
{
	struct fpmi_scoreboard_proc_s proc, *proc_p;

	proc_p = fpmi_scoreboard_proc_acquire(child->wp->scoreboard, child->scoreboard_i, 1);
	if (!proc_p) {
		zlog(ZLOG_WARNING, "failed to acquire scoreboard");
		return;
	}

	proc = *proc_p;
	fpmi_scoreboard_proc_release(proc_p);

#if HAVE_FPMI_TRACE
	if (child->slow_logged.tv_sec) {
		if (child->slow_logged.tv_sec != proc.accepted.tv_sec || child->slow_logged.tv_usec != proc.accepted.tv_usec) {
			child->slow_logged.tv_sec = 0;
			child->slow_logged.tv_usec = 0;
		}
	}
#endif

	if (proc.request_stage > FPMI_REQUEST_ACCEPTING && proc.request_stage < FPMI_REQUEST_END) {
		char purified_script_filename[sizeof(proc.script_filename)];
		struct timeval tv;

		timersub(now, &proc.accepted, &tv);

#if HAVE_FPMI_TRACE
		if (child->slow_logged.tv_sec == 0 && slowlog_timeout &&
				proc.request_stage == FPMI_REQUEST_EXECUTING && tv.tv_sec >= slowlog_timeout) {

			str_purify_filename(purified_script_filename, proc.script_filename, sizeof(proc.script_filename));

			child->slow_logged = proc.accepted;
			child->tracer = fpmi_php_trace;

			fpmi_trace_signal(child->pid);

			zlog(ZLOG_WARNING, "[pool %s] child %d, script '%s' (request: \"%s %s%s%s\") executing too slow (%d.%06d sec), logging",
				child->wp->config->name, (int) child->pid, purified_script_filename, proc.request_method, proc.request_uri,
				(proc.query_string[0] ? "?" : ""), proc.query_string,
				(int) tv.tv_sec, (int) tv.tv_usec);
		}
		else
#endif
		if (terminate_timeout && tv.tv_sec >= terminate_timeout) {
			str_purify_filename(purified_script_filename, proc.script_filename, sizeof(proc.script_filename));
			fpmi_pctl_kill(child->pid, FPMI_PCTL_TERM);

			zlog(ZLOG_WARNING, "[pool %s] child %d, script '%s' (request: \"%s %s%s%s\") execution timed out (%d.%06d sec), terminating",
				child->wp->config->name, (int) child->pid, purified_script_filename, proc.request_method, proc.request_uri,
				(proc.query_string[0] ? "?" : ""), proc.query_string,
				(int) tv.tv_sec, (int) tv.tv_usec);
		}
	}
}
/* }}} */

int fpmi_request_is_idle(struct fpmi_child_s *child) /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;

	/* no need in atomicity here */
	proc = fpmi_scoreboard_proc_get(child->wp->scoreboard, child->scoreboard_i);
	if (!proc) {
		return 0;
	}

	return proc->request_stage == FPMI_REQUEST_ACCEPTING;
}
/* }}} */

int fpmi_request_last_activity(struct fpmi_child_s *child, struct timeval *tv) /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;

	if (!tv) return -1;

	proc = fpmi_scoreboard_proc_get(child->wp->scoreboard, child->scoreboard_i);
	if (!proc) {
		return -1;
	}

	*tv = proc->tv;

	return 1;
}
/* }}} */
