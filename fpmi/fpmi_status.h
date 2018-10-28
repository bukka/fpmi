
	/* (c) 2009 Jerome Loyet */

#ifndef FPMI_STATUS_H
#define FPMI_STATUS_H 1
#include "fpmi_worker_pool.h"
#include "fpmi_shm.h"

#define FPMI_STATUS_BUFFER_SIZE 512

struct fpmi_status_s {
	int pm;
	int idle;
	int active;
	int total;
	unsigned cur_lq;
	int max_lq;
	unsigned long int accepted_conn;
	unsigned int max_children_reached;
	struct timeval last_update;
};

int fpmi_status_init_child(struct fpmi_worker_pool_s *wp);
void fpmi_status_update_activity(struct fpmi_shm_s *shm, int idle, int active, int total, unsigned cur_lq, int max_lq, int clear_last_update);
void fpmi_status_update_accepted_conn(struct fpmi_shm_s *shm, unsigned long int accepted_conn);
void fpmi_status_increment_accepted_conn(struct fpmi_shm_s *shm);
void fpmi_status_set_pm(struct fpmi_shm_s *shm, int pm);
void fpmi_status_update_max_children_reached(struct fpmi_shm_s *shm, unsigned int max_children_reached);
void fpmi_status_increment_max_children_reached(struct fpmi_shm_s *shm);
int fpmi_status_handle_request(void);
int fpmi_status_export_to_zval(zval *status);

extern struct fpmi_shm_s *fpmi_status_shm;

#endif
