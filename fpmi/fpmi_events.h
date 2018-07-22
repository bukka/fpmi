
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_EVENTS_H
#define FPMI_EVENTS_H 1

#define FPMI_EV_TIMEOUT  (1 << 0)
#define FPMI_EV_READ     (1 << 1)
#define FPMI_EV_PERSIST  (1 << 2)
#define FPMI_EV_EDGE     (1 << 3)

#define fpmi_event_set_timer(ev, flags, cb, arg) fpmi_event_set((ev), -1, (flags), (cb), (arg))

struct fpmi_event_s {
	int fd;                   /* not set with FPMI_EV_TIMEOUT */
	struct timeval timeout;   /* next time to trigger */
	struct timeval frequency;
	void (*callback)(struct fpmi_event_s *, short, void *);
	void *arg;
	int flags;
	int index;                /* index of the fd in the ufds array */
	short which;              /* type of event */
};

typedef struct fpmi_event_queue_s {
	struct fpmi_event_queue_s *prev;
	struct fpmi_event_queue_s *next;
	struct fpmi_event_s *ev;
} fpmi_event_queue;

struct fpmi_event_module_s {
	const char *name;
	int support_edge_trigger;
	int (*init)(int max_fd);
	int (*clean)(void);
	int (*wait)(struct fpmi_event_queue_s *queue, unsigned long int timeout);
	int (*add)(struct fpmi_event_s *ev);
	int (*remove)(struct fpmi_event_s *ev);
};

void fpmi_event_loop(int err);
void fpmi_event_fire(struct fpmi_event_s *ev);
int fpmi_event_init_main();
int fpmi_event_set(struct fpmi_event_s *ev, int fd, int flags, void (*callback)(struct fpmi_event_s *, short, void *), void *arg);
int fpmi_event_add(struct fpmi_event_s *ev, unsigned long int timeout);
int fpmi_event_del(struct fpmi_event_s *ev);
int fpmi_event_pre_init(char *machanism);
const char *fpmi_event_machanism_name();
int fpmi_event_support_edge_trigger();

#endif
