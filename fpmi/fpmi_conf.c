
	/* $Id: fpmi_conf.c,v 1.33.2.3 2008/12/13 03:50:29 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# include <stdint.h>
#endif
#ifdef HAVE_GLOB
# ifndef PHP_WIN32
#  include <glob.h>
# else
#  include "win32/glob.h"
# endif
#endif

#include <stdio.h>
#include <unistd.h>

#include "php.h"
#include "zend_ini_scanner.h"
#include "zend_globals.h"
#include "zend_stream.h"
#include "php_syslog.h"

#include "fpmi.h"
#include "fpmi_conf.h"
#include "fpmi_stdio.h"
#include "fpmi_worker_pool.h"
#include "fpmi_cleanup.h"
#include "fpmi_php.h"
#include "fpmi_sockets.h"
#include "fpmi_shm.h"
#include "fpmi_status.h"
#include "fpmi_log.h"
#include "fpmi_events.h"
#include "zlog.h"
#ifdef HAVE_SYSTEMD
#include "fpmi_systemd.h"
#endif


#define STR2STR(a) (a ? a : "undefined")
#define BOOL2STR(a) (a ? "yes" : "no")
#define GO(field) offsetof(struct fpmi_global_config_s, field)
#define WPO(field) offsetof(struct fpmi_worker_pool_config_s, field)

static int fpmi_conf_load_ini_file(char *filename);
static char *fpmi_conf_set_integer(zval *value, void **config, intptr_t offset);
#if 0 /* not used for now */
static char *fpmi_conf_set_long(zval *value, void **config, intptr_t offset);
#endif
static char *fpmi_conf_set_time(zval *value, void **config, intptr_t offset);
static char *fpmi_conf_set_boolean(zval *value, void **config, intptr_t offset);
static char *fpmi_conf_set_string(zval *value, void **config, intptr_t offset);
static char *fpmi_conf_set_log_level(zval *value, void **config, intptr_t offset);
static char *fpmi_conf_set_rlimit_core(zval *value, void **config, intptr_t offset);
static char *fpmi_conf_set_pm(zval *value, void **config, intptr_t offset);
#ifdef HAVE_SYSLOG_H
static char *fpmi_conf_set_syslog_facility(zval *value, void **config, intptr_t offset);
#endif

struct fpmi_global_config_s fpmi_global_config = {
	.daemonize = 1,
#ifdef HAVE_SYSLOG_H
	.syslog_facility = -1,
#endif
	.process_max = 0,
	.process_priority = 64, /* 64 means unset */
#ifdef HAVE_SYSTEMD
	.systemd_watchdog = 0,
	.systemd_interval = -1, /* -1 means not set */
#endif
	.log_buffering = ZLOG_DEFAULT_BUFFERING,
	.log_limit = ZLOG_DEFAULT_LIMIT
};
static struct fpmi_worker_pool_s *current_wp = NULL;
static int ini_recursion = 0;
static char *ini_filename = NULL;
static int ini_lineno = 0;
static char *ini_include = NULL;

/*
 * Please keep the same order as in fpmi_conf.h and in php-fpmi.conf.in
 */
static struct ini_value_parser_s ini_fpmi_global_options[] = {
	{ "pid",                         &fpmi_conf_set_string,          GO(pid_file) },
	{ "error_log",                   &fpmi_conf_set_string,          GO(error_log) },
#ifdef HAVE_SYSLOG_H
	{ "syslog.ident",                &fpmi_conf_set_string,          GO(syslog_ident) },
	{ "syslog.facility",             &fpmi_conf_set_syslog_facility, GO(syslog_facility) },
#endif
	{ "log_buffering",               &fpmi_conf_set_boolean,         GO(log_buffering) },
	{ "log_level",                   &fpmi_conf_set_log_level,       GO(log_level) },
	{ "log_limit",                   &fpmi_conf_set_integer,         GO(log_limit) },
	{ "emergency_restart_threshold", &fpmi_conf_set_integer,         GO(emergency_restart_threshold) },
	{ "emergency_restart_interval",  &fpmi_conf_set_time,            GO(emergency_restart_interval) },
	{ "process_control_timeout",     &fpmi_conf_set_time,            GO(process_control_timeout) },
	{ "process.max",                 &fpmi_conf_set_integer,         GO(process_max) },
	{ "process.priority",            &fpmi_conf_set_integer,         GO(process_priority) },
	{ "daemonize",                   &fpmi_conf_set_boolean,         GO(daemonize) },
	{ "rlimit_files",                &fpmi_conf_set_integer,         GO(rlimit_files) },
	{ "rlimit_core",                 &fpmi_conf_set_rlimit_core,     GO(rlimit_core) },
	{ "events.mechanism",            &fpmi_conf_set_string,          GO(events_mechanism) },
#ifdef HAVE_SYSTEMD
	{ "systemd_interval",            &fpmi_conf_set_time,            GO(systemd_interval) },
#endif
	{ 0, 0, 0 }
};

/*
 * Please keep the same order as in fpmi_conf.h and in php-fpmi.conf.in
 */
static struct ini_value_parser_s ini_fpmi_pool_options[] = {
	{ "prefix",                    &fpmi_conf_set_string,      WPO(prefix) },
	{ "user",                      &fpmi_conf_set_string,      WPO(user) },
	{ "group",                     &fpmi_conf_set_string,      WPO(group) },
	{ "listen",                    &fpmi_conf_set_string,      WPO(listen_address) },
	{ "listen.backlog",            &fpmi_conf_set_integer,     WPO(listen_backlog) },
#ifdef HAVE_FPMI_ACL
	{ "listen.acl_users",          &fpmi_conf_set_string,      WPO(listen_acl_users) },
	{ "listen.acl_groups",         &fpmi_conf_set_string,      WPO(listen_acl_groups) },
#endif
	{ "listen.owner",              &fpmi_conf_set_string,      WPO(listen_owner) },
	{ "listen.group",              &fpmi_conf_set_string,      WPO(listen_group) },
	{ "listen.mode",               &fpmi_conf_set_string,      WPO(listen_mode) },
	{ "listen.allowed_clients",    &fpmi_conf_set_string,      WPO(listen_allowed_clients) },
	{ "process.priority",          &fpmi_conf_set_integer,     WPO(process_priority) },
	{ "pm",                        &fpmi_conf_set_pm,          WPO(pm) },
	{ "pm.max_children",           &fpmi_conf_set_integer,     WPO(pm_max_children) },
	{ "pm.start_servers",          &fpmi_conf_set_integer,     WPO(pm_start_servers) },
	{ "pm.min_spare_servers",      &fpmi_conf_set_integer,     WPO(pm_min_spare_servers) },
	{ "pm.max_spare_servers",      &fpmi_conf_set_integer,     WPO(pm_max_spare_servers) },
	{ "pm.process_idle_timeout",   &fpmi_conf_set_time,        WPO(pm_process_idle_timeout) },
	{ "pm.max_requests",           &fpmi_conf_set_integer,     WPO(pm_max_requests) },
	{ "pm.status_path",            &fpmi_conf_set_string,      WPO(pm_status_path) },
	{ "ping.path",                 &fpmi_conf_set_string,      WPO(ping_path) },
	{ "ping.response",             &fpmi_conf_set_string,      WPO(ping_response) },
	{ "access.log",                &fpmi_conf_set_string,      WPO(access_log) },
	{ "access.format",             &fpmi_conf_set_string,      WPO(access_format) },
	{ "slowlog",                   &fpmi_conf_set_string,      WPO(slowlog) },
	{ "request_slowlog_timeout",   &fpmi_conf_set_time,        WPO(request_slowlog_timeout) },
	{ "request_slowlog_trace_depth", &fpmi_conf_set_integer,     WPO(request_slowlog_trace_depth) },
	{ "request_terminate_timeout", &fpmi_conf_set_time,        WPO(request_terminate_timeout) },
	{ "rlimit_files",              &fpmi_conf_set_integer,     WPO(rlimit_files) },
	{ "rlimit_core",               &fpmi_conf_set_rlimit_core, WPO(rlimit_core) },
	{ "chroot",                    &fpmi_conf_set_string,      WPO(chroot) },
	{ "chdir",                     &fpmi_conf_set_string,      WPO(chdir) },
	{ "catch_workers_output",      &fpmi_conf_set_boolean,     WPO(catch_workers_output) },
	{ "decorate_workers_output",   &fpmi_conf_set_boolean,     WPO(decorate_workers_output) },
	{ "clear_env",                 &fpmi_conf_set_boolean,     WPO(clear_env) },
	{ "security.limit_extensions", &fpmi_conf_set_string,      WPO(security_limit_extensions) },
#ifdef HAVE_APPARMOR
	{ "apparmor_hat",              &fpmi_conf_set_string,      WPO(apparmor_hat) },
#endif
	{ 0, 0, 0 }
};

static int fpmi_conf_is_dir(char *path) /* {{{ */
{
	struct stat sb;

	if (stat(path, &sb) != 0) {
		return 0;
	}

	return (sb.st_mode & S_IFMT) == S_IFDIR;
}
/* }}} */

/*
 * Expands the '$pool' token in a dynamically allocated string
 */
static int fpmi_conf_expand_pool_name(char **value) {
	char *token;

	if (!value || !*value) {
		return 0;
	}

	while (*value && (token = strstr(*value, "$pool"))) {
		char *buf;
		char *p2 = token + strlen("$pool");

		/* If we are not in a pool, we cannot expand this name now */
		if (!current_wp || !current_wp->config  || !current_wp->config->name) {
			return -1;
		}

		/* "aaa$poolbbb" becomes "aaa\0oolbbb" */
		token[0] = '\0';

		/* Build a brand new string with the expanded token */
		spprintf(&buf, 0, "%s%s%s", *value, current_wp->config->name, p2);

		/* Free the previous value and save the new one */
		free(*value);
		*value = strdup(buf);
		efree(buf);
	}

	return 0;
}

static char *fpmi_conf_set_boolean(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	long value_y = !strcasecmp(val, "1");
	long value_n = !strcasecmp(val, "");

	if (!value_y && !value_n) {
		return "invalid boolean value";
	}

	* (int *) ((char *) *config + offset) = value_y ? 1 : 0;
	return NULL;
}
/* }}} */

static char *fpmi_conf_set_string(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char **config_val = (char **) ((char *) *config + offset);

	if (!config_val) {
		return "internal error: NULL value";
	}

	/* Check if there is a previous value to deallocate */
	if (*config_val) {
		free(*config_val);
	}

	*config_val = strdup(Z_STRVAL_P(value));
	if (!*config_val) {
		return "fpmi_conf_set_string(): strdup() failed";
	}
	if (fpmi_conf_expand_pool_name(config_val) == -1) {
		return "Can't use '$pool' when the pool is not defined";
	}

	return NULL;
}
/* }}} */

static char *fpmi_conf_set_integer(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	char *p;

	/* we don't use strtol because we don't want to allow negative values */
	for (p = val; *p; p++) {
		if (p == val && *p == '-') continue;
		if (*p < '0' || *p > '9') {
			return "is not a valid number (greater or equal than zero)";
		}
	}
	* (int *) ((char *) *config + offset) = atoi(val);
	return NULL;
}
/* }}} */

#if 0 /* not used for now */
static char *fpmi_conf_set_long(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	char *p;

	for (p = val; *p; p++) {
		if ( p == val && *p == '-' ) continue;
		if (*p < '0' || *p > '9') {
			return "is not a valid number (greater or equal than zero)";
		}
	}
	* (long int *) ((char *) *config + offset) = atol(val);
	return NULL;
}
/* }}} */
#endif

static char *fpmi_conf_set_time(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	int len = strlen(val);
	char suffix;
	int seconds;
	if (!len) {
		return "invalid time value";
	}

	suffix = val[len-1];
	switch (suffix) {
		case 'm' :
			val[len-1] = '\0';
			seconds = 60 * atoi(val);
			break;
		case 'h' :
			val[len-1] = '\0';
			seconds = 60 * 60 * atoi(val);
			break;
		case 'd' :
			val[len-1] = '\0';
			seconds = 24 * 60 * 60 * atoi(val);
			break;
		case 's' : /* s is the default suffix */
			val[len-1] = '\0';
			suffix = '0';
		default :
			if (suffix < '0' || suffix > '9') {
				return "unknown suffix used in time value";
			}
			seconds = atoi(val);
			break;
	}

	* (int *) ((char *) *config + offset) = seconds;
	return NULL;
}
/* }}} */

static char *fpmi_conf_set_log_level(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	int log_level;

	if (!strcasecmp(val, "debug")) {
		log_level = ZLOG_DEBUG;
	} else if (!strcasecmp(val, "notice")) {
		log_level = ZLOG_NOTICE;
	} else if (!strcasecmp(val, "warning") || !strcasecmp(val, "warn")) {
		log_level = ZLOG_WARNING;
	} else if (!strcasecmp(val, "error")) {
		log_level = ZLOG_ERROR;
	} else if (!strcasecmp(val, "alert")) {
		log_level = ZLOG_ALERT;
	} else {
		return "invalid value for 'log_level'";
	}

	* (int *) ((char *) *config + offset) = log_level;
	return NULL;
}
/* }}} */

#ifdef HAVE_SYSLOG_H
static char *fpmi_conf_set_syslog_facility(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	int *conf = (int *) ((char *) *config + offset);

#ifdef LOG_AUTH
	if (!strcasecmp(val, "AUTH")) {
		*conf = LOG_AUTH;
		return NULL;
	}
#endif

#ifdef LOG_AUTHPRIV
	if (!strcasecmp(val, "AUTHPRIV")) {
		*conf = LOG_AUTHPRIV;
		return NULL;
	}
#endif

#ifdef LOG_CRON
	if (!strcasecmp(val, "CRON")) {
		*conf = LOG_CRON;
		return NULL;
	}
#endif

#ifdef LOG_DAEMON
	if (!strcasecmp(val, "DAEMON")) {
		*conf = LOG_DAEMON;
		return NULL;
	}
#endif

#ifdef LOG_FTP
	if (!strcasecmp(val, "FTP")) {
		*conf = LOG_FTP;
		return NULL;
	}
#endif

#ifdef LOG_KERN
	if (!strcasecmp(val, "KERN")) {
		*conf = LOG_KERN;
		return NULL;
	}
#endif

#ifdef LOG_LPR
	if (!strcasecmp(val, "LPR")) {
		*conf = LOG_LPR;
		return NULL;
	}
#endif

#ifdef LOG_MAIL
	if (!strcasecmp(val, "MAIL")) {
		*conf = LOG_MAIL;
		return NULL;
	}
#endif

#ifdef LOG_NEWS
	if (!strcasecmp(val, "NEWS")) {
		*conf = LOG_NEWS;
		return NULL;
	}
#endif

#ifdef LOG_SYSLOG
	if (!strcasecmp(val, "SYSLOG")) {
		*conf = LOG_SYSLOG;
		return NULL;
	}
#endif

#ifdef LOG_USER
	if (!strcasecmp(val, "USER")) {
		*conf = LOG_USER;
		return NULL;
	}
#endif

#ifdef LOG_UUCP
	if (!strcasecmp(val, "UUCP")) {
		*conf = LOG_UUCP;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL0
	if (!strcasecmp(val, "LOCAL0")) {
		*conf = LOG_LOCAL0;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL1
	if (!strcasecmp(val, "LOCAL1")) {
		*conf = LOG_LOCAL1;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL2
	if (!strcasecmp(val, "LOCAL2")) {
		*conf = LOG_LOCAL2;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL3
	if (!strcasecmp(val, "LOCAL3")) {
		*conf = LOG_LOCAL3;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL4
	if (!strcasecmp(val, "LOCAL4")) {
		*conf = LOG_LOCAL4;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL5
	if (!strcasecmp(val, "LOCAL5")) {
		*conf = LOG_LOCAL5;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL6
	if (!strcasecmp(val, "LOCAL6")) {
		*conf = LOG_LOCAL6;
		return NULL;
	}
#endif

#ifdef LOG_LOCAL7
	if (!strcasecmp(val, "LOCAL7")) {
		*conf = LOG_LOCAL7;
		return NULL;
	}
#endif

	return "invalid value";
}
/* }}} */
#endif

static char *fpmi_conf_set_rlimit_core(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	int *ptr = (int *) ((char *) *config + offset);

	if (!strcasecmp(val, "unlimited")) {
		*ptr = -1;
	} else {
		int int_value;
		void *subconf = &int_value;
		char *error;

		error = fpmi_conf_set_integer(value, &subconf, 0);

		if (error) {
			return error;
		}

		if (int_value < 0) {
			return "must be greater than zero or 'unlimited'";
		}

		*ptr = int_value;
	}

	return NULL;
}
/* }}} */

static char *fpmi_conf_set_pm(zval *value, void **config, intptr_t offset) /* {{{ */
{
	char *val = Z_STRVAL_P(value);
	struct fpmi_worker_pool_config_s  *c = *config;
	if (!strcasecmp(val, "static")) {
		c->pm = PM_STYLE_STATIC;
	} else if (!strcasecmp(val, "dynamic")) {
		c->pm = PM_STYLE_DYNAMIC;
	} else if (!strcasecmp(val, "ondemand")) {
		c->pm = PM_STYLE_ONDEMAND;
	} else {
		return "invalid process manager (static, dynamic or ondemand)";
	}
	return NULL;
}
/* }}} */

static char *fpmi_conf_set_array(zval *key, zval *value, void **config, int convert_to_bool) /* {{{ */
{
	struct key_value_s *kv;
	struct key_value_s ***parent = (struct key_value_s ***) config;
	int b;
	void *subconf = &b;

	kv = malloc(sizeof(*kv));

	if (!kv) {
		return "malloc() failed";
	}

	memset(kv, 0, sizeof(*kv));
	kv->key = strdup(Z_STRVAL_P(key));

	if (!kv->key) {
		free(kv);
		return "fpmi_conf_set_array: strdup(key) failed";
	}

	if (convert_to_bool) {
		char *err = fpmi_conf_set_boolean(value, &subconf, 0);
		if (err) {
			free(kv->key);
			free(kv);
			return err;
		}
		kv->value = strdup(b ? "1" : "0");
	} else {
		kv->value = strdup(Z_STRVAL_P(value));
		if (fpmi_conf_expand_pool_name(&kv->value) == -1) {
			free(kv->key);
			free(kv);
			return "Can't use '$pool' when the pool is not defined";
		}
	}

	if (!kv->value) {
		free(kv->key);
		free(kv);
		return "fpmi_conf_set_array: strdup(value) failed";
	}

	kv->next = **parent;
	**parent = kv;
	return NULL;
}
/* }}} */

static void *fpmi_worker_pool_config_alloc() /* {{{ */
{
	struct fpmi_worker_pool_s *wp;

	wp = fpmi_worker_pool_alloc();

	if (!wp) {
		return 0;
	}

	wp->config = malloc(sizeof(struct fpmi_worker_pool_config_s));

	if (!wp->config) {
		fpmi_worker_pool_free(wp);
		return 0;
	}

	memset(wp->config, 0, sizeof(struct fpmi_worker_pool_config_s));
	wp->config->listen_backlog = FPMI_BACKLOG_DEFAULT;
	wp->config->pm_process_idle_timeout = 10; /* 10s by default */
	wp->config->process_priority = 64; /* 64 means unset */
	wp->config->clear_env = 1;
	wp->config->decorate_workers_output = 1;

	if (!fpmi_worker_all_pools) {
		fpmi_worker_all_pools = wp;
	} else {
		struct fpmi_worker_pool_s *tmp = fpmi_worker_all_pools;
		while (tmp) {
			if (!tmp->next) {
				tmp->next = wp;
				break;
			}
			tmp = tmp->next;
		}
	}

	current_wp = wp;
	return wp->config;
}
/* }}} */

int fpmi_worker_pool_config_free(struct fpmi_worker_pool_config_s *wpc) /* {{{ */
{
	struct key_value_s *kv, *kv_next;

	free(wpc->name);
	free(wpc->prefix);
	free(wpc->user);
	free(wpc->group);
	free(wpc->listen_address);
	free(wpc->listen_owner);
	free(wpc->listen_group);
	free(wpc->listen_mode);
	free(wpc->listen_allowed_clients);
	free(wpc->pm_status_path);
	free(wpc->ping_path);
	free(wpc->ping_response);
	free(wpc->access_log);
	free(wpc->access_format);
	free(wpc->slowlog);
	free(wpc->chroot);
	free(wpc->chdir);
	free(wpc->security_limit_extensions);
#ifdef HAVE_APPARMOR
	free(wpc->apparmor_hat);
#endif

	for (kv = wpc->php_values; kv; kv = kv_next) {
		kv_next = kv->next;
		free(kv->key);
		free(kv->value);
		free(kv);
	}
	for (kv = wpc->php_admin_values; kv; kv = kv_next) {
		kv_next = kv->next;
		free(kv->key);
		free(kv->value);
		free(kv);
	}
	for (kv = wpc->env; kv; kv = kv_next) {
		kv_next = kv->next;
		free(kv->key);
		free(kv->value);
		free(kv);
	}

	return 0;
}
/* }}} */

static int fpmi_evaluate_full_path(char **path, struct fpmi_worker_pool_s *wp, char *default_prefix, int expand) /* {{{ */
{
	char *prefix = NULL;
	char *full_path;

	if (!path || !*path || **path == '/') {
		return 0;
	}

	if (wp && wp->config) {
		prefix = wp->config->prefix;
	}

	/* if the wp prefix is not set */
	if (prefix == NULL) {
		prefix = fpmi_globals.prefix;
	}

	/* if the global prefix is not set */
	if (prefix == NULL) {
		prefix = default_prefix ? default_prefix : PHP_PREFIX;
	}

	if (expand) {
		char *tmp;
		tmp = strstr(*path, "$prefix");
		if (tmp != NULL) {

			if (tmp != *path) {
				zlog(ZLOG_ERROR, "'$prefix' must be use at the beginning of the value");
				return -1;
			}

			if (strlen(*path) > strlen("$prefix")) {
				free(*path);
				tmp = strdup((*path) + strlen("$prefix"));
				*path = tmp;
			} else {
				free(*path);
				*path = NULL;
			}
		}
	}

	if (*path) {
		spprintf(&full_path, 0, "%s/%s", prefix, *path);
		free(*path);
		*path = strdup(full_path);
		efree(full_path);
	} else {
		*path = strdup(prefix);
	}

	if (**path != '/' && wp != NULL && wp->config) {
		return fpmi_evaluate_full_path(path, NULL, default_prefix, expand);
	}
	return 0;
}
/* }}} */

static int fpmi_conf_process_all_pools() /* {{{ */
{
	struct fpmi_worker_pool_s *wp, *wp2;

	if (!fpmi_worker_all_pools) {
		zlog(ZLOG_ERROR, "No pool defined. at least one pool section must be specified in config file");
		return -1;
	}

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {

		/* prefix */
		if (wp->config->prefix && *wp->config->prefix) {
			fpmi_evaluate_full_path(&wp->config->prefix, NULL, NULL, 0);

			if (!fpmi_conf_is_dir(wp->config->prefix)) {
				zlog(ZLOG_ERROR, "[pool %s] the prefix '%s' does not exist or is not a directory", wp->config->name, wp->config->prefix);
				return -1;
			}
		}

		/* alert if user is not set; only if we are root and fpmi is not running with --allow-to-run-as-root */
		if (!wp->config->user && !geteuid() && !fpmi_globals.run_as_root) {
			zlog(ZLOG_ALERT, "[pool %s] user has not been defined", wp->config->name);
			return -1;
		}

		/* listen */
		if (wp->config->listen_address && *wp->config->listen_address) {
			wp->listen_address_domain = fpmi_sockets_domain_from_address(wp->config->listen_address);

			if (wp->listen_address_domain == FPMI_AF_UNIX && *wp->config->listen_address != '/') {
				fpmi_evaluate_full_path(&wp->config->listen_address, wp, NULL, 0);
			}
		} else {
			zlog(ZLOG_ALERT, "[pool %s] no listen address have been defined!", wp->config->name);
			return -1;
		}

		if (wp->config->process_priority != 64 && (wp->config->process_priority < -19 || wp->config->process_priority > 20)) {
			zlog(ZLOG_ERROR, "[pool %s] process.priority must be included into [-19,20]", wp->config->name);
			return -1;
		}

		/* pm */
		if (wp->config->pm != PM_STYLE_STATIC && wp->config->pm != PM_STYLE_DYNAMIC && wp->config->pm != PM_STYLE_ONDEMAND) {
			zlog(ZLOG_ALERT, "[pool %s] the process manager is missing (static, dynamic or ondemand)", wp->config->name);
			return -1;
		}

		/* pm.max_children */
		if (wp->config->pm_max_children < 1) {
			zlog(ZLOG_ALERT, "[pool %s] pm.max_children must be a positive value", wp->config->name);
			return -1;
		}

		/* pm.start_servers, pm.min_spare_servers, pm.max_spare_servers */
		if (wp->config->pm == PM_STYLE_DYNAMIC) {
			struct fpmi_worker_pool_config_s *config = wp->config;

			if (config->pm_min_spare_servers <= 0) {
				zlog(ZLOG_ALERT, "[pool %s] pm.min_spare_servers(%d) must be a positive value", wp->config->name, config->pm_min_spare_servers);
				return -1;
			}

			if (config->pm_max_spare_servers <= 0) {
				zlog(ZLOG_ALERT, "[pool %s] pm.max_spare_servers(%d) must be a positive value", wp->config->name, config->pm_max_spare_servers);
				return -1;
			}

			if (config->pm_min_spare_servers > config->pm_max_children ||
					config->pm_max_spare_servers > config->pm_max_children) {
				zlog(ZLOG_ALERT, "[pool %s] pm.min_spare_servers(%d) and pm.max_spare_servers(%d) cannot be greater than pm.max_children(%d)", wp->config->name, config->pm_min_spare_servers, config->pm_max_spare_servers, config->pm_max_children);
				return -1;
			}

			if (config->pm_max_spare_servers < config->pm_min_spare_servers) {
				zlog(ZLOG_ALERT, "[pool %s] pm.max_spare_servers(%d) must not be less than pm.min_spare_servers(%d)", wp->config->name, config->pm_max_spare_servers, config->pm_min_spare_servers);
				return -1;
			}

			if (config->pm_start_servers <= 0) {
				config->pm_start_servers = config->pm_min_spare_servers + ((config->pm_max_spare_servers - config->pm_min_spare_servers) / 2);
				zlog(ZLOG_NOTICE, "[pool %s] pm.start_servers is not set. It's been set to %d.", wp->config->name, config->pm_start_servers);

			} else if (config->pm_start_servers < config->pm_min_spare_servers || config->pm_start_servers > config->pm_max_spare_servers) {
				zlog(ZLOG_ALERT, "[pool %s] pm.start_servers(%d) must not be less than pm.min_spare_servers(%d) and not greater than pm.max_spare_servers(%d)", wp->config->name, config->pm_start_servers, config->pm_min_spare_servers, config->pm_max_spare_servers);
				return -1;
			}
		} else if (wp->config->pm == PM_STYLE_ONDEMAND) {
			struct fpmi_worker_pool_config_s *config = wp->config;

			if (!fpmi_event_support_edge_trigger()) {
				zlog(ZLOG_ALERT, "[pool %s] ondemand process manager can ONLY be used when events.mechanisme is either epoll (Linux) or kqueue (*BSD).", wp->config->name);
				return -1;
			}

			if (config->pm_process_idle_timeout < 1) {
				zlog(ZLOG_ALERT, "[pool %s] pm.process_idle_timeout(%ds) must be greater than 0s", wp->config->name, config->pm_process_idle_timeout);
				return -1;
			}

			if (config->listen_backlog < FPMI_BACKLOG_DEFAULT) {
				zlog(ZLOG_WARNING, "[pool %s] listen.backlog(%d) was too low for the ondemand process manager. I updated it for you to %d.", wp->config->name, config->listen_backlog, FPMI_BACKLOG_DEFAULT);
				config->listen_backlog = FPMI_BACKLOG_DEFAULT;
			}

			/* certainely useless but proper */
			config->pm_start_servers = 0;
			config->pm_min_spare_servers = 0;
			config->pm_max_spare_servers = 0;
		}

		/* status */
		if (wp->config->pm_status_path && *wp->config->pm_status_path) {
			size_t i;
			char *status = wp->config->pm_status_path;

			if (*status != '/') {
				zlog(ZLOG_ERROR, "[pool %s] the status path '%s' must start with a '/'", wp->config->name, status);
				return -1;
			}

			if (strlen(status) < 2) {
				zlog(ZLOG_ERROR, "[pool %s] the status path '%s' is not long enough", wp->config->name, status);
				return -1;
			}

			for (i = 0; i < strlen(status); i++) {
				if (!isalnum(status[i]) && status[i] != '/' && status[i] != '-' && status[i] != '_' && status[i] != '.') {
					zlog(ZLOG_ERROR, "[pool %s] the status path '%s' must contain only the following characters '[alphanum]/_-.'", wp->config->name, status);
					return -1;
				}
			}
		}

		/* ping */
		if (wp->config->ping_path && *wp->config->ping_path) {
			char *ping = wp->config->ping_path;
			size_t i;

			if (*ping != '/') {
				zlog(ZLOG_ERROR, "[pool %s] the ping path '%s' must start with a '/'", wp->config->name, ping);
				return -1;
			}

			if (strlen(ping) < 2) {
				zlog(ZLOG_ERROR, "[pool %s] the ping path '%s' is not long enough", wp->config->name, ping);
				return -1;
			}

			for (i = 0; i < strlen(ping); i++) {
				if (!isalnum(ping[i]) && ping[i] != '/' && ping[i] != '-' && ping[i] != '_' && ping[i] != '.') {
					zlog(ZLOG_ERROR, "[pool %s] the ping path '%s' must containt only the following characters '[alphanum]/_-.'", wp->config->name, ping);
					return -1;
				}
			}

			if (!wp->config->ping_response) {
				wp->config->ping_response = strdup("pong");
			} else {
				if (strlen(wp->config->ping_response) < 1) {
					zlog(ZLOG_ERROR, "[pool %s] the ping response page '%s' is not long enough", wp->config->name, wp->config->ping_response);
					return -1;
				}
			}
		} else {
			if (wp->config->ping_response) {
				free(wp->config->ping_response);
				wp->config->ping_response = NULL;
			}
		}

		/* access.log, access.format */
		if (wp->config->access_log && *wp->config->access_log) {
			fpmi_evaluate_full_path(&wp->config->access_log, wp, NULL, 0);
			if (!wp->config->access_format) {
				wp->config->access_format = strdup("%R - %u %t \"%m %r\" %s");
			}
		}

		if (wp->config->request_terminate_timeout) {
			fpmi_globals.heartbeat = fpmi_globals.heartbeat ? MIN(fpmi_globals.heartbeat, (wp->config->request_terminate_timeout * 1000) / 3) : (wp->config->request_terminate_timeout * 1000) / 3;
		}

		/* slowlog */
		if (wp->config->slowlog && *wp->config->slowlog) {
			fpmi_evaluate_full_path(&wp->config->slowlog, wp, NULL, 0);
		}

		/* request_slowlog_timeout */
		if (wp->config->request_slowlog_timeout) {
#if HAVE_FPMI_TRACE
			if (! (wp->config->slowlog && *wp->config->slowlog)) {
				zlog(ZLOG_ERROR, "[pool %s] 'slowlog' must be specified for use with 'request_slowlog_timeout'", wp->config->name);
				return -1;
			}
#else
			static int warned = 0;

			if (!warned) {
				zlog(ZLOG_WARNING, "[pool %s] 'request_slowlog_timeout' is not supported on your system",	wp->config->name);
				warned = 1;
			}

			wp->config->request_slowlog_timeout = 0;
#endif

			if (wp->config->slowlog && *wp->config->slowlog) {
				int fd;

				fd = open(wp->config->slowlog, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);

				if (0 > fd) {
					zlog(ZLOG_SYSERROR, "Unable to create or open slowlog(%s)", wp->config->slowlog);
					return -1;
				}
				close(fd);
			}

			fpmi_globals.heartbeat = fpmi_globals.heartbeat ? MIN(fpmi_globals.heartbeat, (wp->config->request_slowlog_timeout * 1000) / 3) : (wp->config->request_slowlog_timeout * 1000) / 3;

			if (wp->config->request_terminate_timeout && wp->config->request_slowlog_timeout > wp->config->request_terminate_timeout) {
				zlog(ZLOG_ERROR, "[pool %s] 'request_slowlog_timeout' (%d) can't be greater than 'request_terminate_timeout' (%d)", wp->config->name, wp->config->request_slowlog_timeout, wp->config->request_terminate_timeout);
				return -1;
			}
		}

		/* request_slowlog_trace_depth */
		if (wp->config->request_slowlog_trace_depth) {
#if HAVE_FPMI_TRACE
			if (! (wp->config->slowlog && *wp->config->slowlog)) {
				zlog(ZLOG_ERROR, "[pool %s] 'slowlog' must be specified for use with 'request_slowlog_trace_depth'", wp->config->name);
				return -1;
			}
#else
			static int warned = 0;

			if (!warned) {
				zlog(ZLOG_WARNING, "[pool %s] 'request_slowlog_trace_depth' is not supported on your system", wp->config->name);
				warned = 1;
			}
#endif

			if (wp->config->request_slowlog_trace_depth <= 0) {
				zlog(ZLOG_ERROR, "[pool %s] 'request_slowlog_trace_depth' (%d) must be a positive value", wp->config->name, wp->config->request_slowlog_trace_depth);
				return -1;
			}
		} else {
			wp->config->request_slowlog_trace_depth = 20;
		}

		/* chroot */
		if (wp->config->chroot && *wp->config->chroot) {

			fpmi_evaluate_full_path(&wp->config->chroot, wp, NULL, 1);

			if (*wp->config->chroot != '/') {
				zlog(ZLOG_ERROR, "[pool %s] the chroot path '%s' must start with a '/'", wp->config->name, wp->config->chroot);
				return -1;
			}

			if (!fpmi_conf_is_dir(wp->config->chroot)) {
				zlog(ZLOG_ERROR, "[pool %s] the chroot path '%s' does not exist or is not a directory", wp->config->name, wp->config->chroot);
				return -1;
			}
		}

		/* chdir */
		if (wp->config->chdir && *wp->config->chdir) {

			fpmi_evaluate_full_path(&wp->config->chdir, wp, NULL, 0);

			if (*wp->config->chdir != '/') {
				zlog(ZLOG_ERROR, "[pool %s] the chdir path '%s' must start with a '/'", wp->config->name, wp->config->chdir);
				return -1;
			}

			if (wp->config->chroot) {
				char *buf;

				spprintf(&buf, 0, "%s/%s", wp->config->chroot, wp->config->chdir);

				if (!fpmi_conf_is_dir(buf)) {
					zlog(ZLOG_ERROR, "[pool %s] the chdir path '%s' within the chroot path '%s' ('%s') does not exist or is not a directory", wp->config->name, wp->config->chdir, wp->config->chroot, buf);
					efree(buf);
					return -1;
				}

				efree(buf);
			} else {
				if (!fpmi_conf_is_dir(wp->config->chdir)) {
					zlog(ZLOG_ERROR, "[pool %s] the chdir path '%s' does not exist or is not a directory", wp->config->name, wp->config->chdir);
					return -1;
				}
			}
		}

		/* security.limit_extensions */
		if (!wp->config->security_limit_extensions) {
			wp->config->security_limit_extensions = strdup(".php .phar");
		}

		if (*wp->config->security_limit_extensions) {
			int nb_ext;
			char *ext;
			char *security_limit_extensions;
			char *limit_extensions;


			/* strdup because strtok(3) alters the string it parses */
			security_limit_extensions = strdup(wp->config->security_limit_extensions);
			limit_extensions = security_limit_extensions;
			nb_ext = 0;

			/* find the number of extensions */
			while (strtok(limit_extensions, " \t")) {
				limit_extensions = NULL;
				nb_ext++;
			}
			free(security_limit_extensions);

			/* if something found */
			if (nb_ext > 0) {

				/* malloc the extension array */
				wp->limit_extensions = malloc(sizeof(char *) * (nb_ext + 1));
				if (!wp->limit_extensions) {
					zlog(ZLOG_ERROR, "[pool %s] unable to malloc extensions array", wp->config->name);
					return -1;
				}

				/* strdup because strtok(3) alters the string it parses */
				security_limit_extensions = strdup(wp->config->security_limit_extensions);
				limit_extensions = security_limit_extensions;
				nb_ext = 0;

				/* parse the string and save the extension in the array */
				while ((ext = strtok(limit_extensions, " \t"))) {
					limit_extensions = NULL;
					wp->limit_extensions[nb_ext++] = strdup(ext);
				}

				/* end the array with NULL in order to parse it */
				wp->limit_extensions[nb_ext] = NULL;
				free(security_limit_extensions);
			}
		}

		/* env[], php_value[], php_admin_values[] */
		if (!wp->config->chroot) {
			struct key_value_s *kv;
			char *options[] = FPMI_PHP_INI_TO_EXPAND;
			char **p;

			for (kv = wp->config->php_values; kv; kv = kv->next) {
				for (p = options; *p; p++) {
					if (!strcasecmp(kv->key, *p)) {
						fpmi_evaluate_full_path(&kv->value, wp, NULL, 0);
					}
				}
			}
			for (kv = wp->config->php_admin_values; kv; kv = kv->next) {
				if (!strcasecmp(kv->key, "error_log") && !strcasecmp(kv->value, "syslog")) {
					continue;
				}
				for (p = options; *p; p++) {
					if (!strcasecmp(kv->key, *p)) {
						fpmi_evaluate_full_path(&kv->value, wp, NULL, 0);
					}
				}
			}
		}
	}

	/* ensure 2 pools do not use the same listening address */
	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		for (wp2 = fpmi_worker_all_pools; wp2; wp2 = wp2->next) {
			if (wp == wp2) {
				continue;
			}

			if (wp->config->listen_address && *wp->config->listen_address && wp2->config->listen_address && *wp2->config->listen_address && !strcmp(wp->config->listen_address, wp2->config->listen_address)) {
				zlog(ZLOG_ERROR, "[pool %s] unable to set listen address as it's already used in another pool '%s'", wp2->config->name, wp->config->name);
				return -1;
			}
		}
	}
	return 0;
}
/* }}} */

int fpmi_conf_unlink_pid() /* {{{ */
{
	if (fpmi_global_config.pid_file) {
		if (0 > unlink(fpmi_global_config.pid_file)) {
			zlog(ZLOG_SYSERROR, "Unable to remove the PID file (%s).", fpmi_global_config.pid_file);
			return -1;
		}
	}
	return 0;
}
/* }}} */

int fpmi_conf_write_pid() /* {{{ */
{
	int fd;

	if (fpmi_global_config.pid_file) {
		char buf[64];
		int len;

		unlink(fpmi_global_config.pid_file);
		fd = creat(fpmi_global_config.pid_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		if (fd < 0) {
			zlog(ZLOG_SYSERROR, "Unable to create the PID file (%s).", fpmi_global_config.pid_file);
			return -1;
		}

		len = sprintf(buf, "%d", (int) fpmi_globals.parent_pid);

		if (len != write(fd, buf, len)) {
			zlog(ZLOG_SYSERROR, "Unable to write to the PID file.");
			close(fd);
			return -1;
		}
		close(fd);
	}
	return 0;
}
/* }}} */

static int fpmi_conf_post_process(int force_daemon) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;

	if (fpmi_global_config.pid_file) {
		fpmi_evaluate_full_path(&fpmi_global_config.pid_file, NULL, PHP_LOCALSTATEDIR, 0);
	}

	if (force_daemon >= 0) {
		/* forced from command line options */
		fpmi_global_config.daemonize = force_daemon;
	}

	fpmi_globals.log_level = fpmi_global_config.log_level;
	zlog_set_level(fpmi_globals.log_level);
	if (fpmi_global_config.log_limit < ZLOG_MIN_LIMIT) {
		zlog(ZLOG_ERROR, "log_limit must be greater than %d", ZLOG_MIN_LIMIT);
		return -1;
	}
	zlog_set_limit(fpmi_global_config.log_limit);
	zlog_set_buffering(fpmi_global_config.log_buffering);

	if (fpmi_global_config.process_max < 0) {
		zlog(ZLOG_ERROR, "process_max can't be negative");
		return -1;
	}

	if (fpmi_global_config.process_priority != 64 && (fpmi_global_config.process_priority < -19 || fpmi_global_config.process_priority > 20)) {
		zlog(ZLOG_ERROR, "process.priority must be included into [-19,20]");
		return -1;
	}

	if (!fpmi_global_config.error_log) {
		fpmi_global_config.error_log = strdup("log/php-fpmi.log");
	}

#ifdef HAVE_SYSTEMD
	if (0 > fpmi_systemd_conf()) {
		return -1;
	}
#endif

#ifdef HAVE_SYSLOG_H
	if (!fpmi_global_config.syslog_ident) {
		fpmi_global_config.syslog_ident = strdup("php-fpmi");
	}

	if (fpmi_global_config.syslog_facility < 0) {
		fpmi_global_config.syslog_facility = LOG_DAEMON;
	}

	if (strcasecmp(fpmi_global_config.error_log, "syslog") != 0)
#endif
	{
		fpmi_evaluate_full_path(&fpmi_global_config.error_log, NULL, PHP_LOCALSTATEDIR, 0);
	}

	if (0 > fpmi_stdio_open_error_log(0)) {
		return -1;
	}

	if (0 > fpmi_event_pre_init(fpmi_global_config.events_mechanism)) {
		return -1;
	}

	if (0 > fpmi_conf_process_all_pools()) {
		return -1;
	}

	if (0 > fpmi_log_open(0)) {
		return -1;
	}

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		if (!wp->config->access_log || !*wp->config->access_log) {
			continue;
		}
		if (0 > fpmi_log_write(wp->config->access_format)) {
			zlog(ZLOG_ERROR, "[pool %s] wrong format for access.format '%s'", wp->config->name, wp->config->access_format);
			return -1;
		}
	}

	return 0;
}
/* }}} */

static void fpmi_conf_cleanup(int which, void *arg) /* {{{ */
{
	free(fpmi_global_config.pid_file);
	free(fpmi_global_config.error_log);
	free(fpmi_global_config.events_mechanism);
	fpmi_global_config.pid_file = 0;
	fpmi_global_config.error_log = 0;
	fpmi_global_config.log_limit = ZLOG_DEFAULT_LIMIT;
#ifdef HAVE_SYSLOG_H
	free(fpmi_global_config.syslog_ident);
	fpmi_global_config.syslog_ident = 0;
#endif
	free(fpmi_globals.config);
}
/* }}} */

static void fpmi_conf_ini_parser_include(char *inc, void *arg) /* {{{ */
{
	char *filename;
	int *error = (int *)arg;
#ifdef HAVE_GLOB
	glob_t g;
#endif
	size_t i;

	if (!inc || !arg) return;
	if (*error) return; /* We got already an error. Switch to the end. */
	spprintf(&filename, 0, "%s", ini_filename);

#ifdef HAVE_GLOB
	{
		g.gl_offs = 0;
		if ((i = glob(inc, GLOB_ERR | GLOB_MARK, NULL, &g)) != 0) {
#ifdef GLOB_NOMATCH
			if (i == GLOB_NOMATCH) {
				zlog(ZLOG_WARNING, "Nothing matches the include pattern '%s' from %s at line %d.", inc, filename, ini_lineno);
				efree(filename);
				return;
			}
#endif /* GLOB_NOMATCH */
			zlog(ZLOG_ERROR, "Unable to globalize '%s' (ret=%zd) from %s at line %d.", inc, i, filename, ini_lineno);
			*error = 1;
			efree(filename);
			return;
		}

		for (i = 0; i < g.gl_pathc; i++) {
			int len = strlen(g.gl_pathv[i]);
			if (len < 1) continue;
			if (g.gl_pathv[i][len - 1] == '/') continue; /* don't parse directories */
			if (0 > fpmi_conf_load_ini_file(g.gl_pathv[i])) {
				zlog(ZLOG_ERROR, "Unable to include %s from %s at line %d", g.gl_pathv[i], filename, ini_lineno);
				*error = 1;
				efree(filename);
				return;
			}
		}
		globfree(&g);
	}
#else /* HAVE_GLOB */
	if (0 > fpmi_conf_load_ini_file(inc)) {
		zlog(ZLOG_ERROR, "Unable to include %s from %s at line %d", inc, filename, ini_lineno);
		*error = 1;
		efree(filename);
		return;
	}
#endif /* HAVE_GLOB */

	efree(filename);
}
/* }}} */

static void fpmi_conf_ini_parser_section(zval *section, void *arg) /* {{{ */
{
	struct fpmi_worker_pool_s *wp;
	struct fpmi_worker_pool_config_s *config;
	int *error = (int *)arg;

	/* switch to global conf */
	if (!strcasecmp(Z_STRVAL_P(section), "global")) {
		current_wp = NULL;
		return;
	}

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		if (!wp->config) continue;
		if (!wp->config->name) continue;
		if (!strcasecmp(wp->config->name, Z_STRVAL_P(section))) {
			/* Found a wp with the same name. Bring it back */
			current_wp = wp;
			return;
		}
	}

	/* it's a new pool */
	config = (struct fpmi_worker_pool_config_s *)fpmi_worker_pool_config_alloc();
	if (!current_wp || !config) {
		zlog(ZLOG_ERROR, "[%s:%d] Unable to alloc a new WorkerPool for worker '%s'", ini_filename, ini_lineno, Z_STRVAL_P(section));
		*error = 1;
		return;
	}
	config->name = strdup(Z_STRVAL_P(section));
	if (!config->name) {
		zlog(ZLOG_ERROR, "[%s:%d] Unable to alloc memory for configuration name for worker '%s'", ini_filename, ini_lineno, Z_STRVAL_P(section));
		*error = 1;
		return;
	}
}
/* }}} */

static void fpmi_conf_ini_parser_entry(zval *name, zval *value, void *arg) /* {{{ */
{
	struct ini_value_parser_s *parser;
	void *config = NULL;

	int *error = (int *)arg;
	if (!value) {
		zlog(ZLOG_ERROR, "[%s:%d] value is NULL for a ZEND_INI_PARSER_ENTRY", ini_filename, ini_lineno);
		*error = 1;
		return;
	}

	if (!strcmp(Z_STRVAL_P(name), "include")) {
		if (ini_include) {
			zlog(ZLOG_ERROR, "[%s:%d] two includes at the same time !", ini_filename, ini_lineno);
			*error = 1;
			return;
		}
		ini_include = strdup(Z_STRVAL_P(value));
		return;
	}

	if (!current_wp) { /* we are in the global section */
		parser = ini_fpmi_global_options;
		config = &fpmi_global_config;
	} else {
		parser = ini_fpmi_pool_options;
		config = current_wp->config;
	}

	for (; parser->name; parser++) {
		if (!strcasecmp(parser->name, Z_STRVAL_P(name))) {
			char *ret;
			if (!parser->parser) {
				zlog(ZLOG_ERROR, "[%s:%d] the parser for entry '%s' is not defined", ini_filename, ini_lineno, parser->name);
				*error = 1;
				return;
			}

			ret = parser->parser(value, &config, parser->offset);
			if (ret) {
				zlog(ZLOG_ERROR, "[%s:%d] unable to parse value for entry '%s': %s", ini_filename, ini_lineno, parser->name, ret);
				*error = 1;
				return;
			}

			/* all is good ! */
			return;
		}
	}

	/* nothing has been found if we got here */
	zlog(ZLOG_ERROR, "[%s:%d] unknown entry '%s'", ini_filename, ini_lineno, Z_STRVAL_P(name));
	*error = 1;
}
/* }}} */

static void fpmi_conf_ini_parser_array(zval *name, zval *key, zval *value, void *arg) /* {{{ */
{
	int *error = (int *)arg;
	char *err = NULL;
	void *config;

	if (!Z_STRVAL_P(key) || !Z_STRVAL_P(value) || !*Z_STRVAL_P(key)) {
		zlog(ZLOG_ERROR, "[%s:%d] Misspelled  array ?", ini_filename, ini_lineno);
		*error = 1;
		return;
	}
	if (!current_wp || !current_wp->config) {
		zlog(ZLOG_ERROR, "[%s:%d] Array are not allowed in the global section", ini_filename, ini_lineno);
		*error = 1;
		return;
	}

	if (!strcmp("env", Z_STRVAL_P(name))) {
		if (!*Z_STRVAL_P(value)) {
			zlog(ZLOG_ERROR, "[%s:%d] empty value", ini_filename, ini_lineno);
			*error = 1;
			return;
		}
		config = (char *)current_wp->config + WPO(env);
		err = fpmi_conf_set_array(key, value, &config, 0);

	} else if (!strcmp("php_value", Z_STRVAL_P(name))) {
		config = (char *)current_wp->config + WPO(php_values);
		err = fpmi_conf_set_array(key, value, &config, 0);

	} else if (!strcmp("php_admin_value", Z_STRVAL_P(name))) {
		config = (char *)current_wp->config + WPO(php_admin_values);
		err = fpmi_conf_set_array(key, value, &config, 0);

	} else if (!strcmp("php_flag", Z_STRVAL_P(name))) {
		config = (char *)current_wp->config + WPO(php_values);
		err = fpmi_conf_set_array(key, value, &config, 1);

	} else if (!strcmp("php_admin_flag", Z_STRVAL_P(name))) {
		config = (char *)current_wp->config + WPO(php_admin_values);
		err = fpmi_conf_set_array(key, value, &config, 1);

	} else {
		zlog(ZLOG_ERROR, "[%s:%d] unknown directive '%s'", ini_filename, ini_lineno, Z_STRVAL_P(name));
		*error = 1;
		return;
	}

	if (err) {
		zlog(ZLOG_ERROR, "[%s:%d] error while parsing '%s[%s]' : %s", ini_filename, ini_lineno, Z_STRVAL_P(name), Z_STRVAL_P(key), err);
		*error = 1;
		return;
	}
}
/* }}} */

static void fpmi_conf_ini_parser(zval *arg1, zval *arg2, zval *arg3, int callback_type, void *arg) /* {{{ */
{
	int *error;

	if (!arg1 || !arg) return;
	error = (int *)arg;
	if (*error) return; /* We got already an error. Switch to the end. */

	switch(callback_type) {
		case ZEND_INI_PARSER_ENTRY:
			fpmi_conf_ini_parser_entry(arg1, arg2, error);
			break;
		case ZEND_INI_PARSER_SECTION:
			fpmi_conf_ini_parser_section(arg1, error);
			break;
		case ZEND_INI_PARSER_POP_ENTRY:
			fpmi_conf_ini_parser_array(arg1, arg3, arg2, error);
			break;
		default:
			zlog(ZLOG_ERROR, "[%s:%d] Unknown INI syntax", ini_filename, ini_lineno);
			*error = 1;
			break;
	}
}
/* }}} */

int fpmi_conf_load_ini_file(char *filename) /* {{{ */
{
	int error = 0;
	char *buf = NULL, *newbuf = NULL;
	int bufsize = 0;
	int fd, n;
	int nb_read = 1;
	char c = '*';

	int ret = 1;

	if (!filename || !filename[0]) {
		zlog(ZLOG_ERROR, "configuration filename is empty");
		return -1;
	}

	fd = open(filename, O_RDONLY, 0);
	if (fd < 0) {
		zlog(ZLOG_SYSERROR, "failed to open configuration file '%s'", filename);
		return -1;
	}

	if (ini_recursion++ > 4) {
		zlog(ZLOG_ERROR, "failed to include more than 5 files recusively");
		close(fd);
		return -1;
	}

	ini_lineno = 0;
	while (nb_read > 0) {
		int tmp;
		ini_lineno++;
		ini_filename = filename;
		for (n = 0; (nb_read = read(fd, &c, sizeof(char))) == sizeof(char) && c != '\n'; n++) {
			if (n == bufsize) {
				bufsize += 1024;
				newbuf = (char*) realloc(buf, sizeof(char) * (bufsize + 2));
				if (newbuf == NULL) {
					ini_recursion--;
					close(fd);
					free(buf);
					return -1;
				}
				buf = newbuf;
			}

			buf[n] = c;
		}
		if (n == 0) {
			continue;
		}
		/* always append newline and null terminate */
		buf[n++] = '\n';
		buf[n] = '\0';
		tmp = zend_parse_ini_string(buf, 1, ZEND_INI_SCANNER_NORMAL, (zend_ini_parser_cb_t)fpmi_conf_ini_parser, &error);
		ini_filename = filename;
		if (error || tmp == FAILURE) {
			if (ini_include) free(ini_include);
			ini_recursion--;
			close(fd);
			free(buf);
			return -1;
		}
		if (ini_include) {
			char *tmp = ini_include;
			ini_include = NULL;
			fpmi_evaluate_full_path(&tmp, NULL, NULL, 0);
			fpmi_conf_ini_parser_include(tmp, &error);
			if (error) {
				free(tmp);
				ini_recursion--;
				close(fd);
				free(buf);
				return -1;
			}
			free(tmp);
		}
	}
	free(buf);

	ini_recursion--;
	close(fd);
	return ret;
}
/* }}} */

static void fpmi_conf_dump() /* {{{ */
{
	struct fpmi_worker_pool_s *wp;

	/*
	 * Please keep the same order as in fpmi_conf.h and in php-fpmi.conf.in
	 */
	zlog(ZLOG_NOTICE, "[global]");
	zlog(ZLOG_NOTICE, "\tpid = %s",                         STR2STR(fpmi_global_config.pid_file));
	zlog(ZLOG_NOTICE, "\terror_log = %s",                   STR2STR(fpmi_global_config.error_log));
#ifdef HAVE_SYSLOG_H
	zlog(ZLOG_NOTICE, "\tsyslog.ident = %s",                STR2STR(fpmi_global_config.syslog_ident));
	zlog(ZLOG_NOTICE, "\tsyslog.facility = %d",             fpmi_global_config.syslog_facility); /* FIXME: convert to string */
#endif
	zlog(ZLOG_NOTICE, "\tlog_buffering = %s",               BOOL2STR(fpmi_global_config.log_buffering));
	zlog(ZLOG_NOTICE, "\tlog_level = %s",                   zlog_get_level_name(fpmi_globals.log_level));
	zlog(ZLOG_NOTICE, "\tlog_limit = %d",                   fpmi_global_config.log_limit);
	zlog(ZLOG_NOTICE, "\tworkers_output_limit = %d",        fpmi_globals.workers_output_limit);
	zlog(ZLOG_NOTICE, "\temergency_restart_interval = %ds", fpmi_global_config.emergency_restart_interval);
	zlog(ZLOG_NOTICE, "\temergency_restart_threshold = %d", fpmi_global_config.emergency_restart_threshold);
	zlog(ZLOG_NOTICE, "\tprocess_control_timeout = %ds",    fpmi_global_config.process_control_timeout);
	zlog(ZLOG_NOTICE, "\tprocess.max = %d",                 fpmi_global_config.process_max);
	if (fpmi_global_config.process_priority == 64) {
		zlog(ZLOG_NOTICE, "\tprocess.priority = undefined");
	} else {
		zlog(ZLOG_NOTICE, "\tprocess.priority = %d", fpmi_global_config.process_priority);
	}
	zlog(ZLOG_NOTICE, "\tdaemonize = %s",                   BOOL2STR(fpmi_global_config.daemonize));
	zlog(ZLOG_NOTICE, "\trlimit_files = %d",                fpmi_global_config.rlimit_files);
	zlog(ZLOG_NOTICE, "\trlimit_core = %d",                 fpmi_global_config.rlimit_core);
	zlog(ZLOG_NOTICE, "\tevents.mechanism = %s",            fpmi_event_machanism_name());
#ifdef HAVE_SYSTEMD
	zlog(ZLOG_NOTICE, "\tsystemd_interval = %ds",           fpmi_global_config.systemd_interval/1000);
#endif
	zlog(ZLOG_NOTICE, " ");

	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		struct key_value_s *kv;
		if (!wp->config) continue;
		zlog(ZLOG_NOTICE, "[%s]",                              STR2STR(wp->config->name));
		zlog(ZLOG_NOTICE, "\tprefix = %s",                     STR2STR(wp->config->prefix));
		zlog(ZLOG_NOTICE, "\tuser = %s",                       STR2STR(wp->config->user));
		zlog(ZLOG_NOTICE, "\tgroup = %s",                      STR2STR(wp->config->group));
		zlog(ZLOG_NOTICE, "\tlisten = %s",                     STR2STR(wp->config->listen_address));
		zlog(ZLOG_NOTICE, "\tlisten.backlog = %d",             wp->config->listen_backlog);
#ifdef HAVE_FPMI_ACL
		zlog(ZLOG_NOTICE, "\tlisten.acl_users = %s",           STR2STR(wp->config->listen_acl_users));
		zlog(ZLOG_NOTICE, "\tlisten.acl_groups = %s",          STR2STR(wp->config->listen_acl_groups));
#endif
		zlog(ZLOG_NOTICE, "\tlisten.owner = %s",               STR2STR(wp->config->listen_owner));
		zlog(ZLOG_NOTICE, "\tlisten.group = %s",               STR2STR(wp->config->listen_group));
		zlog(ZLOG_NOTICE, "\tlisten.mode = %s",                STR2STR(wp->config->listen_mode));
		zlog(ZLOG_NOTICE, "\tlisten.allowed_clients = %s",     STR2STR(wp->config->listen_allowed_clients));
		if (wp->config->process_priority == 64) {
			zlog(ZLOG_NOTICE, "\tprocess.priority = undefined");
		} else {
			zlog(ZLOG_NOTICE, "\tprocess.priority = %d", wp->config->process_priority);
		}
		zlog(ZLOG_NOTICE, "\tpm = %s",                         PM2STR(wp->config->pm));
		zlog(ZLOG_NOTICE, "\tpm.max_children = %d",            wp->config->pm_max_children);
		zlog(ZLOG_NOTICE, "\tpm.start_servers = %d",           wp->config->pm_start_servers);
		zlog(ZLOG_NOTICE, "\tpm.min_spare_servers = %d",       wp->config->pm_min_spare_servers);
		zlog(ZLOG_NOTICE, "\tpm.max_spare_servers = %d",       wp->config->pm_max_spare_servers);
		zlog(ZLOG_NOTICE, "\tpm.process_idle_timeout = %d",    wp->config->pm_process_idle_timeout);
		zlog(ZLOG_NOTICE, "\tpm.max_requests = %d",            wp->config->pm_max_requests);
		zlog(ZLOG_NOTICE, "\tpm.status_path = %s",             STR2STR(wp->config->pm_status_path));
		zlog(ZLOG_NOTICE, "\tping.path = %s",                  STR2STR(wp->config->ping_path));
		zlog(ZLOG_NOTICE, "\tping.response = %s",              STR2STR(wp->config->ping_response));
		zlog(ZLOG_NOTICE, "\taccess.log = %s",                 STR2STR(wp->config->access_log));
		zlog(ZLOG_NOTICE, "\taccess.format = %s",              STR2STR(wp->config->access_format));
		zlog(ZLOG_NOTICE, "\tslowlog = %s",                    STR2STR(wp->config->slowlog));
		zlog(ZLOG_NOTICE, "\trequest_slowlog_timeout = %ds",   wp->config->request_slowlog_timeout);
		zlog(ZLOG_NOTICE, "\trequest_slowlog_trace_depth = %d", wp->config->request_slowlog_trace_depth);
		zlog(ZLOG_NOTICE, "\trequest_terminate_timeout = %ds", wp->config->request_terminate_timeout);
		zlog(ZLOG_NOTICE, "\trlimit_files = %d",               wp->config->rlimit_files);
		zlog(ZLOG_NOTICE, "\trlimit_core = %d",                wp->config->rlimit_core);
		zlog(ZLOG_NOTICE, "\tchroot = %s",                     STR2STR(wp->config->chroot));
		zlog(ZLOG_NOTICE, "\tchdir = %s",                      STR2STR(wp->config->chdir));
		zlog(ZLOG_NOTICE, "\tcatch_workers_output = %s",       BOOL2STR(wp->config->catch_workers_output));
		zlog(ZLOG_NOTICE, "\tdecorate_workers_output = %s",    BOOL2STR(wp->config->decorate_workers_output));
		zlog(ZLOG_NOTICE, "\tclear_env = %s",                  BOOL2STR(wp->config->clear_env));
		zlog(ZLOG_NOTICE, "\tsecurity.limit_extensions = %s",  wp->config->security_limit_extensions);

		for (kv = wp->config->env; kv; kv = kv->next) {
			zlog(ZLOG_NOTICE, "\tenv[%s] = %s", kv->key, kv->value);
		}

		for (kv = wp->config->php_values; kv; kv = kv->next) {
			zlog(ZLOG_NOTICE, "\tphp_value[%s] = %s", kv->key, kv->value);
		}

		for (kv = wp->config->php_admin_values; kv; kv = kv->next) {
			zlog(ZLOG_NOTICE, "\tphp_admin_value[%s] = %s", kv->key, kv->value);
		}
		zlog(ZLOG_NOTICE, " ");
	}
}
/* }}} */

int fpmi_conf_init_main(int test_conf, int force_daemon) /* {{{ */
{
	int ret;

	if (fpmi_globals.prefix && *fpmi_globals.prefix) {
		if (!fpmi_conf_is_dir(fpmi_globals.prefix)) {
			zlog(ZLOG_ERROR, "the global prefix '%s' does not exist or is not a directory", fpmi_globals.prefix);
			return -1;
		}
	}

	if (fpmi_globals.pid && *fpmi_globals.pid) {
		fpmi_global_config.pid_file = strdup(fpmi_globals.pid);
	}

	if (fpmi_globals.config == NULL) {
		char *tmp;

		if (fpmi_globals.prefix == NULL) {
			spprintf(&tmp, 0, "%s/php-fpmi.conf", PHP_SYSCONFDIR);
		} else {
			spprintf(&tmp, 0, "%s/etc/php-fpmi.conf", fpmi_globals.prefix);
		}

		if (!tmp) {
			zlog(ZLOG_SYSERROR, "spprintf() failed (tmp for fpmi_globals.config)");
			return -1;
		}

		fpmi_globals.config = strdup(tmp);
		efree(tmp);

		if (!fpmi_globals.config) {
			zlog(ZLOG_SYSERROR, "spprintf() failed (fpmi_globals.config)");
			return -1;
		}
	}

	ret = fpmi_conf_load_ini_file(fpmi_globals.config);

	if (0 > ret) {
		zlog(ZLOG_ERROR, "failed to load configuration file '%s'", fpmi_globals.config);
		return -1;
	}

	if (0 > fpmi_conf_post_process(force_daemon)) {
		zlog(ZLOG_ERROR, "failed to post process the configuration");
		return -1;
	}

	if (test_conf) {
		if (test_conf > 1) {
			fpmi_conf_dump();
		}
		zlog(ZLOG_NOTICE, "configuration file %s test is successful\n", fpmi_globals.config);
		fpmi_globals.test_successful = 1;
		return -1;
	}

	if (0 > fpmi_cleanup_add(FPMI_CLEANUP_ALL, fpmi_conf_cleanup, 0)) {
		return -1;
	}

	return 0;
}
/* }}} */
