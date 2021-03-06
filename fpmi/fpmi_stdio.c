
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
#include "fpmi_cleanup.h"
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

	return 0;
}
/* }}} */


#define FPMI_STREAM_SET_MSG_PREFIX_FMT "[pool %s] child %d said into %s: "

#define FPMI_STDIO_CMD_FLUSH "\0fscf"

int fpmi_stdio_flush_child() /* {{{ */
{
	return write(STDERR_FILENO, FPMI_STDIO_CMD_FLUSH, sizeof(FPMI_STDIO_CMD_FLUSH));
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
	int in_buf = 0, cmd_pos = 0, pos, start;
	int read_fail = 0, create_log_stream;
	struct zlog_stream *log_stream;

	if (!arg) {
		return;
	}
	child = (struct fpmi_child_s *)arg;

	is_stdout = (fd == child->fd_stdout);
	if (is_stdout) {
		event = &child->ev_stdout;
	} else {
		event = &child->ev_stderr;
	}

	create_log_stream = !child->log_stream;
	if (create_log_stream) {
		log_stream = child->log_stream = malloc(sizeof(struct zlog_stream));
		zlog_stream_init_ex(log_stream, ZLOG_WARNING, STDERR_FILENO);
		zlog_stream_set_decorating(log_stream, child->wp->config->decorate_workers_output);
		zlog_stream_set_wrapping(log_stream, ZLOG_TRUE);
		zlog_stream_set_msg_prefix(log_stream, FPMI_STREAM_SET_MSG_PREFIX_FMT,
				child->wp->config->name, (int) child->pid, is_stdout ? "stdout" : "stderr");
		zlog_stream_set_msg_quoting(log_stream, ZLOG_TRUE);
		zlog_stream_set_is_stdout(log_stream, is_stdout);
		zlog_stream_set_child_pid(log_stream, (int)child->pid);
	} else {
		log_stream = child->log_stream;
		// if fd type (stdout/stderr) or child's pid is changed,
		// then the stream will be finished and msg's prefix will be reinitialized
		if (log_stream->is_stdout != (unsigned int)is_stdout || log_stream->child_pid != (int)child->pid) {
			zlog_stream_finish(log_stream);
			zlog_stream_set_msg_prefix(log_stream, FPMI_STREAM_SET_MSG_PREFIX_FMT,
					child->wp->config->name, (int) child->pid, is_stdout ? "stdout" : "stderr");
			zlog_stream_set_is_stdout(log_stream, is_stdout);
			zlog_stream_set_child_pid(log_stream, (int)child->pid);
		}
	}

	while (1) {
stdio_read:
		in_buf = read(fd, buf, max_buf_size - 1);
		if (in_buf <= 0) { /* no data */
			if (in_buf == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
				/* pipe is closed or error */
				read_fail = (in_buf < 0) ? in_buf : 1;
			}
			break;
		}
		start = 0;
		if (cmd_pos > 0) {
			if 	((sizeof(FPMI_STDIO_CMD_FLUSH) - cmd_pos) <= in_buf &&
					!memcmp(buf, &FPMI_STDIO_CMD_FLUSH[cmd_pos], sizeof(FPMI_STDIO_CMD_FLUSH) - cmd_pos)) {
				zlog_stream_finish(log_stream);
				start = cmd_pos;
			} else {
				zlog_stream_str(log_stream, &FPMI_STDIO_CMD_FLUSH[0], cmd_pos);
			}
			cmd_pos = 0;
		}
		for (pos = start; pos < in_buf; pos++) {
			switch (buf[pos]) {
				case '\n':
					zlog_stream_str(log_stream, buf + start, pos - start);
					zlog_stream_finish(log_stream);
					start = pos + 1;
					break;
				case '\0':
					if (pos + sizeof(FPMI_STDIO_CMD_FLUSH) <= in_buf) {
						if (!memcmp(buf + pos, FPMI_STDIO_CMD_FLUSH, sizeof(FPMI_STDIO_CMD_FLUSH))) {
							zlog_stream_str(log_stream, buf + start, pos - start);
							zlog_stream_finish(log_stream);
							start = pos + sizeof(FPMI_STDIO_CMD_FLUSH);
							pos = start - 1;
						}
					} else if (!memcmp(buf + pos, FPMI_STDIO_CMD_FLUSH, in_buf - pos)) {
						cmd_pos = in_buf - pos;
						zlog_stream_str(log_stream, buf + start, pos - start);
						goto stdio_read;
					}
					break;
			}
		}
		if (start < pos) {
			zlog_stream_str(log_stream, buf + start, pos - start);
		}
	}

	if (read_fail) {
		if (create_log_stream) {
			zlog_stream_set_msg_suffix(log_stream, NULL, ", pipe is closed");
			zlog_stream_finish(log_stream);
		}
		if (read_fail < 0) {
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
#if PHP_VERSION_ID < 70299
#define php_openlog openlog
#endif
	if (!strcasecmp(fpmi_global_config.error_log, "syslog")) {
		php_openlog(fpmi_global_config.syslog_ident, LOG_PID | LOG_CONS, fpmi_global_config.syslog_facility);
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

