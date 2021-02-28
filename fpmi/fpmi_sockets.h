
	/* (c) 2007,2008 Andrei Nigmatulin */

#ifndef FPMI_MISC_H
#define FPMI_MISC_H 1

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

#include "fpmi_worker_pool.h"

/*
  On FreeBSD and OpenBSD, backlog negative values are truncated to SOMAXCONN
*/
#if (__FreeBSD__) || (__OpenBSD__)
#define FPMI_BACKLOG_DEFAULT -1
#else
#define FPMI_BACKLOG_DEFAULT 511 
#endif

#define FPMI_ENV_SOCKET_SET_MAX 256
#define FPMI_ENV_SOCKET_SET_SIZE 128

enum fpmi_address_domain fpmi_sockets_domain_from_address(char *addr);
int fpmi_sockets_init_main();
int fpmi_socket_get_listening_queue(int sock, unsigned *cur_lq, unsigned *max_lq);
int fpmi_socket_unix_test_connect(struct sockaddr_un *sock, size_t socklen);


static inline int fd_set_blocked(int fd, int blocked) /* {{{ */
{
	int flags = fcntl(fd, F_GETFL);

	if (flags < 0) {
		return -1;
	}

	if (blocked) {
		flags &= ~O_NONBLOCK;
	} else {
		flags |= O_NONBLOCK;
	}
	return fcntl(fd, F_SETFL, flags);
}
/* }}} */

#endif
