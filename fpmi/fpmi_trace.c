
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#include <sys/types.h>

#include "fpmi_trace.h"

int fpmi_trace_get_strz(char *buf, size_t sz, long addr) /* {{{ */
{
	int i;
	long l = addr;
	char *lc = (char *) &l;

	i = l % SIZEOF_LONG;
	l -= i;
	for (addr = l; ; addr += SIZEOF_LONG) {
		if (0 > fpmi_trace_get_long(addr, &l)) {
			return -1;
		}
		for ( ; i < SIZEOF_LONG; i++) {
			--sz;
			if (sz && lc[i]) {
				*buf++ = lc[i];
				continue;
			}
			*buf = '\0';
			return 0;
		}
		i = 0;
	}
}
/* }}} */


