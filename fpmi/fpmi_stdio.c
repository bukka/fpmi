
	/* $Id: fpmi_stdio.c,v 1.22.2.2 2008/12/13 03:32:24 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "php_syslog.h"

#include "fpmi.h"
#include "fpmi_children.h"
#include "fpmi_events.h"
#include "fpmi_sockets.h"
#include "fpmi_stdio.h"
#include "zlog.h"

static int fd_stdout[2];
static int fd_stderr[2];

int fpmi_stdio_init_main() /* {{{ */
{
	int fd = open("/dev/null", O_RDWR);

	if (0 > fd) {
		zlog(ZLOG_SYSERROR, "failed to init stdio: open(\"/dev/null\")");
		return -1;
	}

	if (0 > dup2(fd, STDIN_FILENO) || 0 > dup2(fd, STDOUT_FILENO)) {
		zlog(ZLOG_SYSERROR, "failed to init stdio: dup2()");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}
/* }}} */

static inline int fpmi_use_error_log() {  /* {{{ */
	/*
	 * the error_log is NOT used when running in foreground
	 * and from a tty (user looking at output).
	 * So, error_log is used by
	 * - SysV init launch php-fpmi as a daemon
	 * - Systemd launch php-fpmi in foreground
	 */
#if HAVE_UNISTD_H
	if (fpmi_global_config.daemonize || (!isatty(STDERR_FILENO) && !fpmi_globals.force_stderr)) {
#else
	if (fpmi_global_config.daemonize) {
#endif
		return 1;
	}
	return 0;
}

/* }}} */
int fpmi_stdio_init_final() /* {{{ */
{
	if (fpmi_use_error_log()) {
		/* prevent duping if logging to syslog */
		if (fpmi_globals.error_log_fd > 0 && fpmi_globals.error_log_fd != STDERR_FILENO) {

			/* there might be messages to stderr from other parts of the code, we need to log them all */
			if (0 > dup2(fpmi_globals.error_log_fd, STDERR_FILENO)) {
				zlog(ZLOG_SYSERROR, "failed to init stdio: dup2()");
				return -1;
			}
		}
#ifdef HAVE_SYSLOG_H
		else if (fpmi_globals.error_log_fd == ZLOG_SYSLOG) {
			/* dup to /dev/null when using syslog */
			dup2(STDOUT_FILENO, STDERR_FILENO);
		}
#endif
	}
	zlog_set_launched();
	return 0;
}
/* }}} */

int fpmi_stdio_init_child(struct fpmi_worker_pool_s *wp) /* {{{ */
{
#ifdef HAVE_SYSLOG_H
	if (fpmi_globals.error_log_fd == ZLOG_SYSLOG) {
		closelog(); /* ensure to close syslog not to interrupt with PHP syslog code */
	} else
#endif

	/* Notice: child cannot use master error_log
	 * because not aware when being reopen
	 * else, should use if (!fpmi_use_error_log())
	 */
	if (fpmi_globals.error_log_fd > 0) {
		close(fpmi_globals.error_log_fd);
	}
	fpmi_globals.error_log_fd = -1;
	zlog_set_fd(-1);

	if (wp->listening_socket != STDIN_FILENO) {
		if (0 > dup2(wp->listening_socket, STDIN_FILENO)) {
			zlog(ZLOG_SYSERROR, "failed to init child stdio: dup2()");
			return -1;
		}
	}
	return 0;
}
/* }}} */

static void fpmi_stdio_child_said(struct fpmi_event_s *ev, short which, void *arg) /* {{{ */
{
	static const int max_buf_size = 1024;
	int fd = ev->fd;
	char buf[max_buf_size];
	struct fpmi_child_s *child;
	int is_stdout;
	struct fpmi_event_s *event;
	int fifo_in = 1, fifo_out = 1;
	int in_buf = 0;
	int res;
	int init_stream_prefix_and_suffix = 1;
	struct zlog_stream stream;

	if (!arg) {
		return;
	}
	child = (struct fpmi_child_s *)arg;

	zlog_stream_init_ex(&stream, ZLOG_WARNING, STDERR_FILENO);
	zlog_stream_set_decorating(&stream, child->wp->config->decorate_workers_output);
	zlog_stream_set_wrapping(&stream, ZLOG_TRUE);

	is_stdout = (fd == child->fd_stdout);
	if (is_stdout) {
		event = &child->ev_stdout;
	} else {
		event = &child->ev_stderr;
	}

	while (fifo_in || fifo_out) {
		if (fifo_in) {
			res = read(fd, buf + in_buf, max_buf_size - 1 - in_buf);
			if (res <= 0) { /* no data */
				fifo_in = 0;
				if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
					/* just no more data ready */
				} else { /* error or pipe is closed */

					if (res < 0) { /* error */
						zlog(ZLOG_SYSERROR, "unable to read what child say");
					}

					fpmi_event_del(event);

					if (is_stdout) {
						close(child->fd_stdout);
						child->fd_stdout = -1;
					} else {
						close(child->fd_stderr);
						child->fd_stderr = -1;
					}
				}
			} else {
				in_buf += res;
			}
		}

		if (fifo_out) {
			if (in_buf == 0) {
				fifo_out = 0;
			} else {
				char *nl;
				int should_print = 0;
				buf[in_buf] = '\0';

				/* FIXME: there might be binary data */

				/* we should print if no more space in the buffer */
				if (in_buf == max_buf_size - 1) {
					should_print = 1;
				}

				/* we should print if no more data to come */
				if (!fifo_in) {
					should_print = 1;
				}

				nl = strchr(buf, '\n');
				if (nl || should_print) {

					if (nl) {
						*nl = '\0';
					}

					if (init_stream_prefix_and_suffix) {
						zlog_stream_set_msg_prefix(&stream, "[pool %s] child %d said into %s: ",
								child->wp->config->name, (int) child->pid, is_stdout ? "stdout" : "stderr");
						zlog_stream_set_msg_suffix(&stream, NULL, ", pipe is closed");
						zlog_stream_set_msg_quoting(&stream, ZLOG_TRUE);
						init_stream_prefix_and_suffix = 0;
					}
					zlog_stream_str(&stream, buf, strlen(buf));

					if (nl) {
						int out_buf = 1 + nl - buf;
						memmove(buf, buf + out_buf, in_buf - out_buf);
						in_buf -= out_buf;
					} else {
						in_buf = 0;
					}
				}
			}
		}
	}
	zlog_stream_close(&stream);
}
/* }}} */

int fpmi_stdio_prepare_pipes(struct fpmi_child_s *child) /* {{{ */
{
	if (0 == child->wp->config->catch_workers_output) { /* not required */
		return 0;
	}

	if (0 > pipe(fd_stdout)) {
		zlog(ZLOG_SYSERROR, "failed to prepare the stdout pipe");
		return -1;
	}

	if (0 > pipe(fd_stderr)) {
		zlog(ZLOG_SYSERROR, "failed to prepare the stderr pipe");
		close(fd_stdout[0]);
		close(fd_stdout[1]);
		return -1;
	}

	if (0 > fd_set_blocked(fd_stdout[0], 0) || 0 > fd_set_blocked(fd_stderr[0], 0)) {
		zlog(ZLOG_SYSERROR, "failed to unblock pipes");
		close(fd_stdout[0]);
		close(fd_stdout[1]);
		close(fd_stderr[0]);
		close(fd_stderr[1]);
		return -1;
	}
	return 0;
}
/* }}} */

int fpmi_stdio_parent_use_pipes(struct fpmi_child_s *child) /* {{{ */
{
	if (0 == child->wp->config->catch_workers_output) { /* not required */
		return 0;
	}

	close(fd_stdout[1]);
	close(fd_stderr[1]);

	child->fd_stdout = fd_stdout[0];
	child->fd_stderr = fd_stderr[0];

	fpmi_event_set(&child->ev_stdout, child->fd_stdout, FPMI_EV_READ, fpmi_stdio_child_said, child);
	fpmi_event_add(&child->ev_stdout, 0);

	fpmi_event_set(&child->ev_stderr, child->fd_stderr, FPMI_EV_READ, fpmi_stdio_child_said, child);
	fpmi_event_add(&child->ev_stderr, 0);
	return 0;
}
/* }}} */

int fpmi_stdio_discard_pipes(struct fpmi_child_s *child) /* {{{ */
{
	if (0 == child->wp->config->catch_workers_output) { /* not required */
		return 0;
	}

	close(fd_stdout[1]);
	close(fd_stderr[1]);

	close(fd_stdout[0]);
	close(fd_stderr[0]);
	return 0;
}
/* }}} */

void fpmi_stdio_child_use_pipes(struct fpmi_child_s *child) /* {{{ */
{
	if (child->wp->config->catch_workers_output) {
		dup2(fd_stdout[1], STDOUT_FILENO);
		dup2(fd_stderr[1], STDERR_FILENO);
		close(fd_stdout[0]); close(fd_stdout[1]);
		close(fd_stderr[0]); close(fd_stderr[1]);
	} else {
		/* stdout of parent is always /dev/null */
		dup2(STDOUT_FILENO, STDERR_FILENO);
	}
}
/* }}} */

int fpmi_stdio_open_error_log(int reopen) /* {{{ */
{
	int fd;

#ifdef HAVE_SYSLOG_H
	if (!strcasecmp(fpmi_global_config.error_log, "syslog")) {
		openlog(fpmi_global_config.syslog_ident, LOG_PID | LOG_CONS, fpmi_global_config.syslog_facility);
		fpmi_globals.error_log_fd = ZLOG_SYSLOG;
		if (fpmi_use_error_log()) {
			zlog_set_fd(fpmi_globals.error_log_fd);
		}
		return 0;
	}
#endif

	fd = open(fpmi_global_config.error_log, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
	if (0 > fd) {
		zlog(ZLOG_SYSERROR, "failed to open error_log (%s)", fpmi_global_config.error_log);
		return -1;
	}

	if (reopen) {
		if (fpmi_use_error_log()) {
			dup2(fd, STDERR_FILENO);
		}

		dup2(fd, fpmi_globals.error_log_fd);
		close(fd);
		fd = fpmi_globals.error_log_fd; /* for FD_CLOSEXEC to work */
	} else {
		fpmi_globals.error_log_fd = fd;
		if (fpmi_use_error_log()) {
			zlog_set_fd(fpmi_globals.error_log_fd);
		}
	}
	if (0 > fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC)) {
		zlog(ZLOG_WARNING, "failed to change attribute of error_log");
	}
	return 0;
}
/* }}} */

