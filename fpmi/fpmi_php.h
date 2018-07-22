
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_PHP_H
#define FPMI_PHP_H 1

#include <TSRM.h>

#include "php.h"
#include "build-defs.h" /* for PHP_ defines */
#include "fpmi/fpmi_conf.h"

#define FPMI_PHP_INI_TO_EXPAND \
	{ \
		"error_log", \
		"extension_dir", \
		"mime_magic.magicfile", \
		"sendmail_path", \
		"session.cookie_path", \
		"session_pgsql.sem_file_name", \
		"soap.wsdl_cache_dir", \
		"uploadprogress.file.filename_template", \
		"xdebug.output_dir", \
		"xdebug.profiler_output_dir", \
		"xdebug.trace_output_dir", \
		"xmms.path", \
		"axis2.client_home", \
		"blenc.key_file", \
		"coin_acceptor.device", \
		NULL \
	}

struct fpmi_worker_pool_s;

int fpmi_php_init_child(struct fpmi_worker_pool_s *wp);
char *fpmi_php_script_filename(void);
char *fpmi_php_request_uri(void);
char *fpmi_php_request_method(void);
char *fpmi_php_query_string(void);
char *fpmi_php_auth_user(void);
size_t fpmi_php_content_length(void);
void fpmi_php_soft_quit();
int fpmi_php_init_main();
int fpmi_php_apply_defines_ex(struct key_value_s *kv, int mode);
int fpmi_php_limit_extensions(char *path);
char* fpmi_php_get_string_from_table(zend_string *table, char *key);

#endif

