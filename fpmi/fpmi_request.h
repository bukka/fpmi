
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_REQUEST_H
#define FPMI_REQUEST_H 1

void fpmi_request_accepting();				/* hanging in accept() */
void fpmi_request_reading_headers();			/* start reading fastcgi request from very first byte */
void fpmi_request_info();					/* not a stage really but a point in the php code, where all request params have become known to sapi */
void fpmi_request_executing();				/* the script is executing */
void fpmi_request_end(void);				/* request ended: script response have been sent to web server */
void fpmi_request_finished();				/* request processed: cleaning current request */

struct fpmi_child_s;
struct timeval;

void fpmi_request_check_timed_out(struct fpmi_child_s *child, struct timeval *tv, int terminate_timeout, int slowlog_timeout);
int fpmi_request_is_idle(struct fpmi_child_s *child);
const char *fpmi_request_get_stage_name(int stage);
int fpmi_request_last_activity(struct fpmi_child_s *child, struct timeval *tv);

enum fpmi_request_stage_e {
	FPMI_REQUEST_ACCEPTING = 1,
	FPMI_REQUEST_READING_HEADERS,
	FPMI_REQUEST_INFO,
	FPMI_REQUEST_EXECUTING,
	FPMI_REQUEST_END,
	FPMI_REQUEST_FINISHED
};

#endif
