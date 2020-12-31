
	/* (c) 2009 Jerome Loyet */

#include "php.h"
#include "SAPI.h"
#include <stdio.h>

#include "fpmi_config.h"
#include "fpmi_scoreboard.h"
#include "fpmi_status.h"
#include "fpmi_clock.h"
#include "fpmi_scoreboard.h"
#include "zlog.h"
#include "fpmi_atomic.h"
#include "fpmi_conf.h"
#include "fpmi_php.h"
#include <ext/standard/html.h>

static char *fpmi_status_uri = NULL;
static char *fpmi_status_ping_uri = NULL;
static char *fpmi_status_ping_response = NULL;


int fpmi_status_init_child(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	if (!wp || !wp->config) {
		zlog(ZLOG_ERROR, "unable to init fpmi_status because conf structure is NULL");
		return -1;
	}

	if (wp->config->pm_status_path) {
		fpmi_status_uri = strdup(wp->config->pm_status_path);
	}

	if (wp->config->ping_path) {
		if (!wp->config->ping_response) {
			zlog(ZLOG_ERROR, "[pool %s] ping is set (%s) but ping.response is not set.", wp->config->name, wp->config->ping_path);
			return -1;
		}
		fpmi_status_ping_uri = strdup(wp->config->ping_path);
		fpmi_status_ping_response = strdup(wp->config->ping_response);
	}

	return 0;
}
/* }}} */

int fpmi_status_export_to_zval(zval *status)
{
	struct fpmi_scoreboard_s scoreboard, *scoreboard_p;
	zval fpmi_proc_stats, fpmi_proc_stat;
	time_t now_epoch;
	struct timeval duration, now;
	double cpu;
	int i;

	scoreboard_p = fpmi_scoreboard_get();
	if (scoreboard_p->shared) {
		/* use shared scoreboard if available */
		scoreboard_p = scoreboard_p->shared;
	}

	scoreboard_p = fpmi_scoreboard_acquire(scoreboard_p, 1);
	if (!scoreboard_p) {
		zlog(ZLOG_NOTICE, "[pool %s] status: scoreboard already in use.", scoreboard_p->pool);
		return -1;
	}

	/* copy the scoreboard not to bother other processes */
	scoreboard = *scoreboard_p;
	struct fpmi_scoreboard_proc_s procs[scoreboard.nprocs];

	struct fpmi_scoreboard_proc_s *proc_p;
	for(i=0; i < scoreboard.nprocs; i++) {
		proc_p = fpmi_scoreboard_proc_acquire(scoreboard_p, i, 1);
		if (!proc_p){
			procs[i].used=-1;
			continue;
		}
		procs[i] = *proc_p;
		fpmi_scoreboard_proc_release(proc_p);
	}
	fpmi_scoreboard_release(scoreboard_p);

	now_epoch = time(NULL);
	fpmi_clock_get(&now);

	array_init(status);
	add_assoc_string(status, "pool", scoreboard.pool);
	add_assoc_string(status, "process-manager", PM2STR(scoreboard.pm));
	add_assoc_long(status, "start-time", scoreboard.start_epoch);
	add_assoc_long(status, "start-since", now_epoch - scoreboard.start_epoch);
	add_assoc_long(status, "accepted-conn", scoreboard.requests);
#ifdef HAVE_FPMI_LQ
	add_assoc_long(status, "listen-queue", scoreboard.lq);
	add_assoc_long(status, "max-listen-queue", scoreboard.lq_max);
	add_assoc_long(status, "listen-queue-len", scoreboard.lq_len);
#endif
	add_assoc_long(status, "idle-processes", scoreboard.idle);
	add_assoc_long(status, "active-processes", scoreboard.active);
	add_assoc_long(status, "total-processes", scoreboard.idle + scoreboard.active);
	add_assoc_long(status, "max-active-processes", scoreboard.active_max);
	add_assoc_long(status, "max-children-reached", scoreboard.max_children_reached);
	add_assoc_long(status, "slow-requests", scoreboard.slow_rq);

	array_init(&fpmi_proc_stats);
	for(i=0; i<scoreboard.nprocs; i++) {
		if (!procs[i].used) {
			continue;
		}
		proc_p = &procs[i];
#ifdef HAVE_FPMI_LQ
		/* prevent NaN */
		if (procs[i].cpu_duration.tv_sec == 0 && procs[i].cpu_duration.tv_usec == 0) {
			cpu = 0.;
		} else {
			cpu = (procs[i].last_request_cpu.tms_utime + procs[i].last_request_cpu.tms_stime + procs[i].last_request_cpu.tms_cutime + procs[i].last_request_cpu.tms_cstime) / fpmi_scoreboard_get_tick() / (procs[i].cpu_duration.tv_sec + procs[i].cpu_duration.tv_usec / 1000000.) * 100.;
		}
#endif

		array_init(&fpmi_proc_stat);
		add_assoc_long(&fpmi_proc_stat, "pid", procs[i].pid);
		add_assoc_string(&fpmi_proc_stat, "state", fpmi_request_get_stage_name(procs[i].request_stage));
		add_assoc_long(&fpmi_proc_stat, "start-time", procs[i].start_epoch);
		add_assoc_long(&fpmi_proc_stat, "start-since", now_epoch - procs[i].start_epoch);
		add_assoc_long(&fpmi_proc_stat, "requests", procs[i].requests);
		if (procs[i].request_stage == FPMI_REQUEST_ACCEPTING) {
			duration = procs[i].duration;
		} else {
			timersub(&now, &procs[i].accepted, &duration);
		}
		add_assoc_long(&fpmi_proc_stat, "request-duration", duration.tv_sec * 1000000UL + duration.tv_usec);
		add_assoc_string(&fpmi_proc_stat, "request-method", procs[i].request_method[0] != '\0' ? procs[i].request_method : "-");
		add_assoc_string(&fpmi_proc_stat, "request-uri", procs[i].request_uri);
		add_assoc_string(&fpmi_proc_stat, "query-string", procs[i].query_string);
		add_assoc_long(&fpmi_proc_stat, "request-length", procs[i].content_length);
		add_assoc_string(&fpmi_proc_stat, "user", procs[i].auth_user[0] != '\0' ? procs[i].auth_user : "-");
		add_assoc_string(&fpmi_proc_stat, "script", procs[i].script_filename[0] != '\0' ? procs[i].script_filename : "-");
#ifdef HAVE_FPMI_LQ
		add_assoc_double(&fpmi_proc_stat, "last-request-cpu", procs[i].request_stage == FPMI_REQUEST_ACCEPTING ? cpu : 0.);
#endif
		add_assoc_long(&fpmi_proc_stat, "last-request-memory", procs[i].request_stage == FPMI_REQUEST_ACCEPTING ? procs[i].memory : 0);
		add_next_index_zval(&fpmi_proc_stats, &fpmi_proc_stat);
	}
	add_assoc_zval(status, "procs", &fpmi_proc_stats);
	return 0;
}
/* }}} */

int fpmi_status_handle_request(void) /* {{{ */
{
	struct fpmi_scoreboard_s scoreboard, *scoreboard_p;
	struct fpmi_scoreboard_proc_s proc;
	char *buffer, *time_format, time_buffer[64];
	time_t now_epoch;
	int full, encode;
	char *short_syntax, *short_post;
	char *full_pre, *full_syntax, *full_post, *full_separator;
	zend_string *_GET_str;

	if (!SG(request_info).request_uri) {
		return 0;
	}

	/* PING */
	if (fpmi_status_ping_uri && fpmi_status_ping_response && !strcmp(fpmi_status_ping_uri, SG(request_info).request_uri)) {
		fpmi_request_executing();
		sapi_add_header_ex(ZEND_STRL("Content-Type: text/plain"), 1, 1);
		sapi_add_header_ex(ZEND_STRL("Expires: Thu, 01 Jan 1970 00:00:00 GMT"), 1, 1);
		sapi_add_header_ex(ZEND_STRL("Cache-Control: no-cache, no-store, must-revalidate, max-age=0"), 1, 1);
		SG(sapi_headers).http_response_code = 200;

		/* handle HEAD */
		if (SG(request_info).headers_only) {
			return 1;
		}

		PUTS(fpmi_status_ping_response);
		return 1;
	}

	/* STATUS */
	if (fpmi_status_uri && !strcmp(fpmi_status_uri, SG(request_info).request_uri)) {
		fpmi_request_executing();

		scoreboard_p = fpmi_scoreboard_get();
		if (scoreboard_p->shared) {
			scoreboard_p = scoreboard_p->shared;
		}
		if (!scoreboard_p) {
			zlog(ZLOG_ERROR, "status: unable to find or access status shared memory");
			SG(sapi_headers).http_response_code = 500;
			sapi_add_header_ex(ZEND_STRL("Content-Type: text/plain"), 1, 1);
			sapi_add_header_ex(ZEND_STRL("Expires: Thu, 01 Jan 1970 00:00:00 GMT"), 1, 1);
			sapi_add_header_ex(ZEND_STRL("Cache-Control: no-cache, no-store, must-revalidate, max-age=0"), 1, 1);
			PUTS("Internal error. Please review log file for errors.");
			return 1;
		}

		if (!fpmi_spinlock(&scoreboard_p->lock, 1)) {
			zlog(ZLOG_NOTICE, "[pool %s] status: scoreboard already in used.", scoreboard_p->pool);
			SG(sapi_headers).http_response_code = 503;
			sapi_add_header_ex(ZEND_STRL("Content-Type: text/plain"), 1, 1);
			sapi_add_header_ex(ZEND_STRL("Expires: Thu, 01 Jan 1970 00:00:00 GMT"), 1, 1);
			sapi_add_header_ex(ZEND_STRL("Cache-Control: no-cache, no-store, must-revalidate, max-age=0"), 1, 1);
			PUTS("Server busy. Please try again later.");
			return 1;
		}
		/* copy the scoreboard not to bother other processes */
		scoreboard = *scoreboard_p;
		fpmi_unlock(scoreboard_p->lock);

		if (scoreboard.idle < 0 || scoreboard.active < 0) {
			zlog(ZLOG_ERROR, "[pool %s] invalid status values", scoreboard.pool);
			SG(sapi_headers).http_response_code = 500;
			sapi_add_header_ex(ZEND_STRL("Content-Type: text/plain"), 1, 1);
			sapi_add_header_ex(ZEND_STRL("Expires: Thu, 01 Jan 1970 00:00:00 GMT"), 1, 1);
			sapi_add_header_ex(ZEND_STRL("Cache-Control: no-cache, no-store, must-revalidate, max-age=0"), 1, 1);
			PUTS("Internal error. Please review log file for errors.");
			return 1;
		}

		/* send common headers */
		sapi_add_header_ex(ZEND_STRL("Expires: Thu, 01 Jan 1970 00:00:00 GMT"), 1, 1);
		sapi_add_header_ex(ZEND_STRL("Cache-Control: no-cache, no-store, must-revalidate, max-age=0"), 1, 1);
		SG(sapi_headers).http_response_code = 200;

		/* handle HEAD */
		if (SG(request_info).headers_only) {
			return 1;
		}

		/* full status ? */
		_GET_str = zend_string_init("_GET", sizeof("_GET")-1, 0);
		full = (fpmi_php_get_string_from_table(_GET_str, "full") != NULL);
		short_syntax = short_post = NULL;
		full_separator = full_pre = full_syntax = full_post = NULL;
		encode = 0;

		/* HTML */
		if (fpmi_php_get_string_from_table(_GET_str, "html")) {
			sapi_add_header_ex(ZEND_STRL("Content-Type: text/html"), 1, 1);
			time_format = "%d/%b/%Y:%H:%M:%S %z";
			encode = 1;

			short_syntax =
				"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
				"<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
				"<head><title>PHP-FPMI Status Page</title></head>\n"
				"<body>\n"
				"<table>\n"
					"<tr><th>pool</th><td>%s</td></tr>\n"
					"<tr><th>process manager</th><td>%s</td></tr>\n"
					"<tr><th>start time</th><td>%s</td></tr>\n"
					"<tr><th>start since</th><td>%lu</td></tr>\n"
					"<tr><th>accepted conn</th><td>%lu</td></tr>\n"
#ifdef HAVE_FPMI_LQ
					"<tr><th>listen queue</th><td>%u</td></tr>\n"
					"<tr><th>max listen queue</th><td>%u</td></tr>\n"
					"<tr><th>listen queue len</th><td>%d</td></tr>\n"
#endif
					"<tr><th>idle processes</th><td>%d</td></tr>\n"
					"<tr><th>active processes</th><td>%d</td></tr>\n"
					"<tr><th>total processes</th><td>%d</td></tr>\n"
					"<tr><th>max active processes</th><td>%d</td></tr>\n"
					"<tr><th>max children reached</th><td>%u</td></tr>\n"
					"<tr><th>slow requests</th><td>%lu</td></tr>\n"
				"</table>\n";

			if (!full) {
				short_post = "</body></html>";
			} else {
				full_pre =
					"<table border=\"1\">\n"
					"<tr>"
						"<th>pid</th>"
						"<th>state</th>"
						"<th>start time</th>"
						"<th>start since</th>"
						"<th>requests</th>"
						"<th>request duration</th>"
						"<th>request method</th>"
						"<th>request uri</th>"
						"<th>content length</th>"
						"<th>user</th>"
						"<th>script</th>"
#ifdef HAVE_FPMI_LQ
						"<th>last request cpu</th>"
#endif
						"<th>last request memory</th>"
					"</tr>\n";

				full_syntax =
					"<tr>"
						"<td>%d</td>"
						"<td>%s</td>"
						"<td>%s</td>"
						"<td>%lu</td>"
						"<td>%lu</td>"
						"<td>%lu</td>"
						"<td>%s</td>"
						"<td>%s%s%s</td>"
						"<td>%zu</td>"
						"<td>%s</td>"
						"<td>%s</td>"
#ifdef HAVE_FPMI_LQ
						"<td>%.2f</td>"
#endif
						"<td>%zu</td>"
					"</tr>\n";

				full_post = "</table></body></html>";
			}

		/* XML */
		} else if (fpmi_php_get_string_from_table(_GET_str, "xml")) {
			sapi_add_header_ex(ZEND_STRL("Content-Type: text/xml"), 1, 1);
			time_format = "%s";
			encode = 1;

			short_syntax =
				"<?xml version=\"1.0\" ?>\n"
				"<status>\n"
				"<pool>%s</pool>\n"
				"<process-manager>%s</process-manager>\n"
				"<start-time>%s</start-time>\n"
				"<start-since>%lu</start-since>\n"
				"<accepted-conn>%lu</accepted-conn>\n"
#ifdef HAVE_FPMI_LQ
				"<listen-queue>%u</listen-queue>\n"
				"<max-listen-queue>%u</max-listen-queue>\n"
				"<listen-queue-len>%d</listen-queue-len>\n"
#endif
				"<idle-processes>%d</idle-processes>\n"
				"<active-processes>%d</active-processes>\n"
				"<total-processes>%d</total-processes>\n"
				"<max-active-processes>%d</max-active-processes>\n"
				"<max-children-reached>%u</max-children-reached>\n"
				"<slow-requests>%lu</slow-requests>\n";

				if (!full) {
					short_post = "</status>";
				} else {
					full_pre = "<processes>\n";
					full_syntax =
						"<process>"
							"<pid>%d</pid>"
							"<state>%s</state>"
							"<start-time>%s</start-time>"
							"<start-since>%lu</start-since>"
							"<requests>%lu</requests>"
							"<request-duration>%lu</request-duration>"
							"<request-method>%s</request-method>"
							"<request-uri>%s%s%s</request-uri>"
							"<content-length>%zu</content-length>"
							"<user>%s</user>"
							"<script>%s</script>"
#ifdef HAVE_FPMI_LQ
							"<last-request-cpu>%.2f</last-request-cpu>"
#endif
							"<last-request-memory>%zu</last-request-memory>"
						"</process>\n"
					;
					full_post = "</processes>\n</status>";
				}

			/* JSON */
		} else if (fpmi_php_get_string_from_table(_GET_str, "json")) {
			sapi_add_header_ex(ZEND_STRL("Content-Type: application/json"), 1, 1);
			time_format = "%s";

			short_syntax =
				"{"
				"\"pool\":\"%s\","
				"\"process manager\":\"%s\","
				"\"start time\":%s,"
				"\"start since\":%lu,"
				"\"accepted conn\":%lu,"
#ifdef HAVE_FPMI_LQ
				"\"listen queue\":%u,"
				"\"max listen queue\":%u,"
				"\"listen queue len\":%d,"
#endif
				"\"idle processes\":%d,"
				"\"active processes\":%d,"
				"\"total processes\":%d,"
				"\"max active processes\":%d,"
				"\"max children reached\":%u,"
				"\"slow requests\":%lu";

			if (!full) {
				short_post = "}";
			} else {
				full_separator = ",";
				full_pre = ", \"processes\":[";

				full_syntax = "{"
					"\"pid\":%d,"
					"\"state\":\"%s\","
					"\"start time\":%s,"
					"\"start since\":%lu,"
					"\"requests\":%lu,"
					"\"request duration\":%lu,"
					"\"request method\":\"%s\","
					"\"request uri\":\"%s%s%s\","
					"\"content length\":%zu,"
					"\"user\":\"%s\","
					"\"script\":\"%s\","
#ifdef HAVE_FPMI_LQ
					"\"last request cpu\":%.2f,"
#endif
					"\"last request memory\":%zu"
					"}";

				full_post = "]}";
			}

		/* TEXT */
		} else {
			sapi_add_header_ex(ZEND_STRL("Content-Type: text/plain"), 1, 1);
			time_format = "%d/%b/%Y:%H:%M:%S %z";

			short_syntax =
				"pool:                 %s\n"
				"process manager:      %s\n"
				"start time:           %s\n"
				"start since:          %lu\n"
				"accepted conn:        %lu\n"
#ifdef HAVE_FPMI_LQ
				"listen queue:         %u\n"
				"max listen queue:     %u\n"
				"listen queue len:     %d\n"
#endif
				"idle processes:       %d\n"
				"active processes:     %d\n"
				"total processes:      %d\n"
				"max active processes: %d\n"
				"max children reached: %u\n"
				"slow requests:        %lu\n";

				if (full) {
					full_syntax =
						"\n"
						"************************\n"
						"pid:                  %d\n"
						"state:                %s\n"
						"start time:           %s\n"
						"start since:          %lu\n"
						"requests:             %lu\n"
						"request duration:     %lu\n"
						"request method:       %s\n"
						"request URI:          %s%s%s\n"
						"content length:       %zu\n"
						"user:                 %s\n"
						"script:               %s\n"
#ifdef HAVE_FPMI_LQ
						"last request cpu:     %.2f\n"
#endif
						"last request memory:  %zu\n";
				}
		}

		strftime(time_buffer, sizeof(time_buffer) - 1, time_format, localtime(&scoreboard.start_epoch));
		now_epoch = time(NULL);
		spprintf(&buffer, 0, short_syntax,
				scoreboard.pool,
				PM2STR(scoreboard.pm),
				time_buffer,
				now_epoch - scoreboard.start_epoch,
				scoreboard.requests,
#ifdef HAVE_FPMI_LQ
				scoreboard.lq,
				scoreboard.lq_max,
				scoreboard.lq_len,
#endif
				scoreboard.idle,
				scoreboard.active,
				scoreboard.idle + scoreboard.active,
				scoreboard.active_max,
				scoreboard.max_children_reached,
				scoreboard.slow_rq);

		PUTS(buffer);
		efree(buffer);
		zend_string_release(_GET_str);

		if (short_post) {
			PUTS(short_post);
		}

		/* no need to test the var 'full' */
		if (full_syntax) {
			unsigned int i;
			int first;
			zend_string *tmp_query_string;
			char *query_string;
			struct timeval duration, now;
#ifdef HAVE_FPMI_LQ
			float cpu;
#endif

			fpmi_clock_get(&now);

			if (full_pre) {
				PUTS(full_pre);
			}

			first = 1;
			for (i=0; i<scoreboard_p->nprocs; i++) {
				if (!scoreboard_p->procs[i] || !scoreboard_p->procs[i]->used) {
					continue;
				}
				proc = *scoreboard_p->procs[i];

				if (first) {
					first = 0;
				} else {
					if (full_separator) {
						PUTS(full_separator);
					}
				}

				query_string = NULL;
				tmp_query_string = NULL;
				if (proc.query_string[0] != '\0') {
					if (!encode) {
						query_string = proc.query_string;
					} else {
						tmp_query_string = php_escape_html_entities_ex(
								(unsigned char *)proc.query_string, 
								strlen(proc.query_string), 1, ENT_HTML_IGNORE_ERRORS & ENT_COMPAT, NULL, 1
#if PHP_VERSION_ID >= 80000
								, 0 /* quiet */
#endif
							);
						query_string = ZSTR_VAL(tmp_query_string);
					}
				}

#ifdef HAVE_FPMI_LQ
				/* prevent NaN */
				if (proc.cpu_duration.tv_sec == 0 && proc.cpu_duration.tv_usec == 0) {
					cpu = 0.;
				} else {
					cpu = (proc.last_request_cpu.tms_utime + proc.last_request_cpu.tms_stime + proc.last_request_cpu.tms_cutime + proc.last_request_cpu.tms_cstime) / fpmi_scoreboard_get_tick() / (proc.cpu_duration.tv_sec + proc.cpu_duration.tv_usec / 1000000.) * 100.;
				}
#endif

				if (proc.request_stage == FPMI_REQUEST_ACCEPTING) {
					duration = proc.duration;
				} else {
					timersub(&now, &proc.accepted, &duration);
				}
				strftime(time_buffer, sizeof(time_buffer) - 1, time_format, localtime(&proc.start_epoch));
				spprintf(&buffer, 0, full_syntax,
					proc.pid,
					fpmi_request_get_stage_name(proc.request_stage),
					time_buffer,
					now_epoch - proc.start_epoch,
					proc.requests,
					duration.tv_sec * 1000000UL + duration.tv_usec,
					proc.request_method[0] != '\0' ? proc.request_method : "-",
					proc.request_uri[0] != '\0' ? proc.request_uri : "-",
					query_string ? "?" : "",
					query_string ? query_string : "",
					proc.content_length,
					proc.auth_user[0] != '\0' ? proc.auth_user : "-",
					proc.script_filename[0] != '\0' ? proc.script_filename : "-",
#ifdef HAVE_FPMI_LQ
					proc.request_stage == FPMI_REQUEST_ACCEPTING ? cpu : 0.,
#endif
					proc.request_stage == FPMI_REQUEST_ACCEPTING ? proc.memory : 0);
				PUTS(buffer);
				efree(buffer);

				if (tmp_query_string) {
					zend_string_free(tmp_query_string);
				}
			}

			if (full_post) {
				PUTS(full_post);
			}
		}

		return 1;
	}

	return 0;
}
/* }}} */

