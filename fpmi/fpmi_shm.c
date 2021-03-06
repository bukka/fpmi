
	/* (c) 2007,2008 Andrei Nigmatulin, Jerome Loyet */

#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include "fpmi_shm.h"
#include "zlog.h"


/* MAP_ANON is deprecated, but not in macosx */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

static size_t fpmi_shm_size = 0;

void *fpmi_shm_alloc(size_t size) /* {{{ */
{
	void *mem;

	mem = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

#ifdef MAP_FAILED
	if (mem == MAP_FAILED) {
		zlog(ZLOG_SYSERROR, "unable to allocate %zu bytes in shared memory: %s", size, strerror(errno));
		return NULL;
	}
#endif

	if (!mem) {
		zlog(ZLOG_SYSERROR, "unable to allocate %zu bytes in shared memory", size);
		return NULL;
	}

	fpmi_shm_size += size;
	return mem;
}
/* }}} */

int fpmi_shm_free(void *mem, size_t size) /* {{{ */
{
	if (!mem) {
		zlog(ZLOG_ERROR, "mem is NULL");
		return 0;
	}

	if (munmap(mem, size) == -1) {
		zlog(ZLOG_SYSERROR, "Unable to free shm");
		return 0;
	}

	if (fpmi_shm_size - size > 0) {
		fpmi_shm_size -= size;
	} else {
		fpmi_shm_size = 0;
	}

	return 1;
}
/* }}} */

size_t fpmi_shm_get_size_allocated() /* {{{*/
{
	return fpmi_shm_size;
}
/* }}} */
