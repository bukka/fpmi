
	/* (c) 2009 Jerome Loyet */

#include "php.h"
#include "SAPI.h"
#include <stdio.h>
#include <time.h>

#include "fpmi_config.h"
#include "fpmi_scoreboard.h"
#include "fpmi_shm.h"
#include "fpmi_sockets.h"
#include "fpmi_worker_pool.h"
#include "fpmi_clock.h"
#include "zlog.h"

static struct fpmi_scoreboard_s *fpmi_scoreboard = NULL;
static int fpmi_scoreboard_i = -1;
#ifdef HAVE_TIMES
static float fpmi_scoreboard_tick;
#endif


int fpmi_scoreboard_init_main() /* {{{ */
{
	struct fpmi_worker_pool_s *wp;
	unsigned int i;

#ifdef HAVE_TIMES
#if (defined(HAVE_SYSCONF) && defined(_SC_CLK_TCK))
	fpmi_scoreboard_tick = sysconf(_SC_CLK_TCK);
#else /* _SC_CLK_TCK */
#ifdef HZ
	fpmi_scoreboard_tick = HZ;
#else /* HZ */
	fpmi_scoreboard_tick = 100;
#endif /* HZ */
#endif /* _SC_CLK_TCK */
	zlog(ZLOG_DEBUG, "got clock tick '%.0f'", fpmi_scoreboard_tick);
#endif /* HAVE_TIMES */


	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		size_t scoreboard_size, scoreboard_nprocs_size;
		void *shm_mem;

		if (wp->config->pm_max_children < 1) {
			zlog(ZLOG_ERROR, "[pool %s] Unable to create scoreboard SHM because max_client is not set", wp->config->name);
			return -1;
		}

		if (wp->scoreboard) {
			zlog(ZLOG_ERROR, "[pool %s] Unable to create scoreboard SHM because it already exists", wp->config->name);
			return -1;
		}

		if (wp->shared) {
			wp->scoreboard = wp->shared->scoreboard;
		}

		scoreboard_size        = sizeof(struct fpmi_scoreboard_s) + (wp->config->pm_max_children) * sizeof(struct fpmi_scoreboard_proc_s *);
		scoreboard_nprocs_size = sizeof(struct fpmi_scoreboard_proc_s) * wp->config->pm_max_children;
		shm_mem                = fpmi_shm_alloc(scoreboard_size + scoreboard_nprocs_size);

		if (!shm_mem) {
			return -1;
		}
		wp->scoreboard         = shm_mem;
		wp->scoreboard->nprocs = wp->config->pm_max_children;
		shm_mem               += scoreboard_size;

		for (i = 0; i < wp->scoreboard->nprocs; i++, shm_mem += sizeof(struct fpmi_scoreboard_proc_s)) {
			wp->scoreboard->procs[i] = shm_mem;
		}

		wp->scoreboard->pm          = wp->config->pm;
		wp->scoreboard->start_epoch = time(NULL);
		strlcpy(wp->scoreboard->pool, wp->config->name, sizeof(wp->scoreboard->pool));
	}
	return 0;
}
/* }}} */

void fpmi_scoreboard_update(int idle, int active, int lq, int lq_len, int requests, int max_children_reached, int slow_rq, int action, struct fpmi_scoreboard_s *scoreboard) /* {{{ */
{
	if (!scoreboard) {
		scoreboard = fpmi_scoreboard;
	}
	if (!scoreboard) {
		zlog(ZLOG_WARNING, "Unable to update scoreboard: the SHM has not been found");
		return;
	}


	fpmi_spinlock(&scoreboard->lock, 0);
	if (action == FPMI_SCOREBOARD_ACTION_SET) {
		if (idle >= 0) {
			scoreboard->idle = idle;
		}
		if (active >= 0) {
			scoreboard->active = active;
		}
		if (lq >= 0) {
			scoreboard->lq = lq;
		}
		if (lq_len >= 0) {
			scoreboard->lq_len = lq_len;
		}
#ifdef HAVE_FPMI_LQ /* prevent unnecessary test */
		if (scoreboard->lq > scoreboard->lq_max) {
			scoreboard->lq_max = scoreboard->lq;
		}
#endif
		if (requests >= 0) {
			scoreboard->requests = requests;
		}

		if (max_children_reached >= 0) {
			scoreboard->max_children_reached = max_children_reached;
		}
		if (slow_rq > 0) {
			scoreboard->slow_rq = slow_rq;
		}
	} else {
		if (scoreboard->idle + idle > 0) {
			scoreboard->idle += idle;
		} else {
			scoreboard->idle = 0;
		}

		if (scoreboard->active + active > 0) {
			scoreboard->active += active;
		} else {
			scoreboard->active = 0;
		}

		if (scoreboard->requests + requests > 0) {
			scoreboard->requests += requests;
		} else {
			scoreboard->requests = 0;
		}

		if (scoreboard->max_children_reached + max_children_reached > 0) {
			scoreboard->max_children_reached += max_children_reached;
		} else {
			scoreboard->max_children_reached = 0;
		}

		if (scoreboard->slow_rq + slow_rq > 0) {
			scoreboard->slow_rq += slow_rq;
		} else {
			scoreboard->slow_rq = 0;
		}
	}

	if (scoreboard->active > scoreboard->active_max) {
		scoreboard->active_max = scoreboard->active;
	}

	fpmi_unlock(scoreboard->lock);
}
/* }}} */

struct fpmi_scoreboard_s *fpmi_scoreboard_get() /* {{{*/
{
	return fpmi_scoreboard;
}
/* }}} */

struct fpmi_scoreboard_proc_s *fpmi_scoreboard_proc_get(struct fpmi_scoreboard_s *scoreboard, int child_index) /* {{{*/
{
	if (!scoreboard) {
		scoreboard = fpmi_scoreboard;
	}

	if (!scoreboard) {
		return NULL;
	}

	if (child_index < 0) {
		child_index = fpmi_scoreboard_i;
	}

	if (child_index < 0 || (unsigned int)child_index >= scoreboard->nprocs) {
		return NULL;
	}

	return scoreboard->procs[child_index];
}
/* }}} */

struct fpmi_scoreboard_s *fpmi_scoreboard_acquire(struct fpmi_scoreboard_s *scoreboard, int nohang) /* {{{ */
{
	struct fpmi_scoreboard_s *s;

	s = scoreboard ? scoreboard : fpmi_scoreboard;
	if (!s) {
		return NULL;
	}

	if (!fpmi_spinlock(&s->lock, nohang)) {
		return NULL;
	}
	return s;
}
/* }}} */

void fpmi_scoreboard_release(struct fpmi_scoreboard_s *scoreboard) {
	if (!scoreboard) {
		return;
	}

	scoreboard->lock = 0;
}

struct fpmi_scoreboard_proc_s *fpmi_scoreboard_proc_acquire(struct fpmi_scoreboard_s *scoreboard, int child_index, int nohang) /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;

	proc = fpmi_scoreboard_proc_get(scoreboard, child_index);
	if (!proc) {
		return NULL;
	}

	if (!fpmi_spinlock(&proc->lock, nohang)) {
		return NULL;
	}

	return proc;
}
/* }}} */

void fpmi_scoreboard_proc_release(struct fpmi_scoreboard_proc_s *proc) /* {{{ */
{
	if (!proc) {
		return;
	}

	proc->lock = 0;
}

void fpmi_scoreboard_free(struct fpmi_scoreboard_s *scoreboard) /* {{{ */
{
	size_t scoreboard_size, scoreboard_nprocs_size;

	if (!scoreboard) {
		zlog(ZLOG_ERROR, "**scoreboard is NULL");
		return;
	}

	scoreboard_size        = sizeof(struct fpmi_scoreboard_s) + (scoreboard->nprocs) * sizeof(struct fpmi_scoreboard_proc_s *);
	scoreboard_nprocs_size = sizeof(struct fpmi_scoreboard_proc_s) * scoreboard->nprocs;

	fpmi_shm_free(scoreboard, scoreboard_size + scoreboard_nprocs_size);
}
/* }}} */

void fpmi_scoreboard_child_use(struct fpmi_scoreboard_s *scoreboard, int child_index, pid_t pid) /* {{{ */
{
	struct fpmi_scoreboard_proc_s *proc;
	fpmi_scoreboard = scoreboard;
	fpmi_scoreboard_i = child_index;
	proc = fpmi_scoreboard_proc_get(scoreboard, child_index);
	if (!proc) {
		return;
	}
	proc->pid = pid;
	proc->start_epoch = time(NULL);
}
/* }}} */

void fpmi_scoreboard_proc_free(struct fpmi_scoreboard_s *scoreboard, int child_index) /* {{{ */
{
	if (!scoreboard) {
		return;
	}

	if (child_index < 0 || (unsigned int)child_index >= scoreboard->nprocs) {
		return;
	}

	if (scoreboard->procs[child_index] && scoreboard->procs[child_index]->used > 0) {
		memset(scoreboard->procs[child_index], 0, sizeof(struct fpmi_scoreboard_proc_s));
	}

	/* set this slot as free to avoid search on next alloc */
	scoreboard->free_proc = child_index;
}
/* }}} */

int fpmi_scoreboard_proc_alloc(struct fpmi_scoreboard_s *scoreboard, int *child_index) /* {{{ */
{
	int i = -1;

	if (!scoreboard || !child_index) {
		return -1;
	}

	/* first try the slot which is supposed to be free */
	if (scoreboard->free_proc >= 0 && (unsigned int)scoreboard->free_proc < scoreboard->nprocs) {
		if (scoreboard->procs[scoreboard->free_proc] && !scoreboard->procs[scoreboard->free_proc]->used) {
			i = scoreboard->free_proc;
		}
	}

	if (i < 0) { /* the supposed free slot is not, let's search for a free slot */
		zlog(ZLOG_DEBUG, "[pool %s] the proc->free_slot was not free. Let's search", scoreboard->pool);
		for (i = 0; i < (int)scoreboard->nprocs; i++) {
			if (scoreboard->procs[i] && !scoreboard->procs[i]->used) { /* found */
				break;
			}
		}
	}

	/* no free slot */
	if (i < 0 || i >= (int)scoreboard->nprocs) {
		zlog(ZLOG_ERROR, "[pool %s] no free scoreboard slot", scoreboard->pool);
		return -1;
	}

	scoreboard->procs[i]->used = 1;
	*child_index = i;

	/* supposed next slot is free */
	if (i + 1 >= (int)scoreboard->nprocs) {
		scoreboard->free_proc = 0;
	} else {
		scoreboard->free_proc = i + 1;
	}

	return 0;
}
/* }}} */

#ifdef HAVE_TIMES
float fpmi_scoreboard_get_tick() /* {{{ */
{
	return fpmi_scoreboard_tick;
}
/* }}} */
#endif

