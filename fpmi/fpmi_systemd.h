#ifndef FPMI_SYSTEMD_H
#define FPMI_SYSTEMD_H 1

#include "fpmi_events.h"

/* 10s (in ms) heartbeat for systemd status */
#define FPMI_SYSTEMD_DEFAULT_HEARTBEAT (10000)

void fpmi_systemd_heartbeat(struct fpmi_event_s *ev, short which, void *arg);
int fpmi_systemd_conf();

#endif

