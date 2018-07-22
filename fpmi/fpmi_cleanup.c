
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <stdlib.h>

#include "fpmi_arrays.h"
#include "fpmi_cleanup.h"

struct cleanup_s {
	int type;
	void (*cleanup)(int, void *);
	void *arg;
};

static struct fpmi_array_s cleanups = { .sz = sizeof(struct cleanup_s) };

int fpmi_cleanup_add(int type, void (*cleanup)(int, void *), void *arg) /* {{{ */
{
	struct cleanup_s *c;

	c = fpmi_array_push(&cleanups);

	if (!c) {
		return -1;
	}

	c->type = type;
	c->cleanup = cleanup;
	c->arg = arg;

	return 0;
}
/* }}} */

void fpmi_cleanups_run(int type) /* {{{ */
{
	struct cleanup_s *c = fpmi_array_item_last(&cleanups);
	int cl = cleanups.used;

	for ( ; cl--; c--) {
		if (c->type & type) {
			c->cleanup(type, c->arg);
		}
	}

	fpmi_array_free(&cleanups);
}
/* }}} */

