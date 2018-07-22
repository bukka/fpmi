
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpmi_config.h"

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <sys/types.h>
#include <sys/stat.h> /* for chmod(2) */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "zlog.h"
#include "fpmi_arrays.h"
#include "fpmi_sockets.h"
#include "fpmi_worker_pool.h"
#include "fpmi_unix.h"
#include "fpmi_str.h"
#include "fpmi_env.h"
#include "fpmi_cleanup.h"
#include "fpmi_scoreboard.h"

struct listening_socket_s {
	int refcount;
	int sock;
	int type;
	char *key;
};

static struct fpmi_array_s sockets_list;

enum { FPMI_GET_USE_SOCKET = 1, FPMI_STORE_SOCKET = 2, FPMI_STORE_USE_SOCKET = 3 };

static void fpmi_sockets_cleanup(int which, void *arg) /* {{{ */
{
	unsigned i;
	char *env_value = 0;
	int p = 0;
	struct listening_socket_s *ls = sockets_list.data;

	for (i = 0; i < sockets_list.used; i++, ls++) {
		if (which != FPMI_CLEANUP_PARENT_EXEC) {
			close(ls->sock);
		} else { /* on PARENT EXEC we want socket fds to be inherited through environment variable */
			char fd[32];
			sprintf(fd, "%d", ls->sock);
			env_value = realloc(env_value, p + (p ? 1 : 0) + strlen(ls->key) + 1 + strlen(fd) + 1);
			p += sprintf(env_value + p, "%s%s=%s", p ? "," : "", ls->key, fd);
		}

		if (which == FPMI_CLEANUP_PARENT_EXIT_MAIN) {
			if (ls->type == FPMI_AF_UNIX) {
				unlink(ls->key);
			}
		}
		free(ls->key);
	}

	if (env_value) {
		setenv("FPMI_SOCKETS", env_value, 1);
		free(env_value);
	}

	fpmi_array_free(&sockets_list);
}
/* }}} */

static void *fpmi_get_in_addr(struct sockaddr *sa) /* {{{ */
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
/* }}} */

static int fpmi_get_in_port(struct sockaddr *sa) /* {{{ */
{
    if (sa->sa_family == AF_INET) {
        return ntohs(((struct sockaddr_in*)sa)->sin_port);
    }

    return ntohs(((struct sockaddr_in6*)sa)->sin6_port);
}
/* }}} */

static int fpmi_sockets_hash_op(int sock, struct sockaddr *sa, char *key, int type, int op) /* {{{ */
{
	if (key == NULL) {
		switch (type) {
			case FPMI_AF_INET : {
				key = alloca(INET6_ADDRSTRLEN+10);
				inet_ntop(sa->sa_family, fpmi_get_in_addr(sa), key, INET6_ADDRSTRLEN);
				sprintf(key+strlen(key), ":%d", fpmi_get_in_port(sa));
				break;
			}

			case FPMI_AF_UNIX : {
				struct sockaddr_un *sa_un = (struct sockaddr_un *) sa;
				key = alloca(strlen(sa_un->sun_path) + 1);
				strcpy(key, sa_un->sun_path);
				break;
			}

			default :
				return -1;
		}
	}

	switch (op) {

		case FPMI_GET_USE_SOCKET :
		{
			unsigned i;
			struct listening_socket_s *ls = sockets_list.data;

			for (i = 0; i < sockets_list.used; i++, ls++) {
				if (!strcmp(ls->key, key)) {
					++ls->refcount;
					return ls->sock;
				}
			}
			break;
		}

		case FPMI_STORE_SOCKET :			/* inherited socket */
		case FPMI_STORE_USE_SOCKET :		/* just created */
		{
			struct listening_socket_s *ls;

			ls = fpmi_array_push(&sockets_list);
			if (!ls) {
				break;
			}

			if (op == FPMI_STORE_SOCKET) {
				ls->refcount = 0;
			} else {
				ls->refcount = 1;
			}
			ls->type = type;
			ls->sock = sock;
			ls->key = strdup(key);

			return 0;
		}
	}
	return -1;
}
/* }}} */

static int fpmi_sockets_new_listening_socket(struct fpmi_worker_pool_s *wp, struct sockaddr *sa, int socklen) /* {{{ */
{
	int flags = 1;
	int sock;
	mode_t saved_umask = 0;

	sock = socket(sa->sa_family, SOCK_STREAM, 0);

	if (0 > sock) {
		zlog(ZLOG_SYSERROR, "failed to create new listening socket: socket()");
		return -1;
	}

	if (0 > setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags))) {
		zlog(ZLOG_WARNING, "failed to change socket attribute");
	}

	if (wp->listen_address_domain == FPMI_AF_UNIX) {
		if (fpmi_socket_unix_test_connect((struct sockaddr_un *)sa, socklen) == 0) {
			zlog(ZLOG_ERROR, "An another FPMI instance seems to already listen on %s", ((struct sockaddr_un *) sa)->sun_path);
			close(sock);
			return -1;
		}
		unlink( ((struct sockaddr_un *) sa)->sun_path);
		saved_umask = umask(0777 ^ wp->socket_mode);
	}

	if (0 > bind(sock, sa, socklen)) {
		zlog(ZLOG_SYSERROR, "unable to bind listening socket for address '%s'", wp->config->listen_address);
		if (wp->listen_address_domain == FPMI_AF_UNIX) {
			umask(saved_umask);
		}
		close(sock);
		return -1;
	}

	if (wp->listen_address_domain == FPMI_AF_UNIX) {
		char *path = ((struct sockaddr_un *) sa)->sun_path;

		umask(saved_umask);

		if (0 > fpmi_unix_set_socket_premissions(wp, path)) {
			close(sock);
			return -1;
		}
	}

	if (0 > listen(sock, wp->config->listen_backlog)) {
		zlog(ZLOG_SYSERROR, "failed to listen to address '%s'", wp->config->listen_address);
		close(sock);
		return -1;
	}

	return sock;
}
/* }}} */

static int fpmi_sockets_get_listening_socket(struct fpmi_worker_pool_s *wp, struct sockaddr *sa, int socklen) /* {{{ */
{
	int sock;

	sock = fpmi_sockets_hash_op(0, sa, 0, wp->listen_address_domain, FPMI_GET_USE_SOCKET);
	if (sock >= 0) {
		return sock;
	}

	sock = fpmi_sockets_new_listening_socket(wp, sa, socklen);
	fpmi_sockets_hash_op(sock, sa, 0, wp->listen_address_domain, FPMI_STORE_USE_SOCKET);

	return sock;
}
/* }}} */

enum fpmi_address_domain fpmi_sockets_domain_from_address(char *address) /* {{{ */
{
	if (strchr(address, ':')) {
		return FPMI_AF_INET;
	}

	if (strlen(address) == strspn(address, "0123456789")) {
		return FPMI_AF_INET;
	}
	return FPMI_AF_UNIX;
}
/* }}} */

static int fpmi_socket_af_inet_socket_by_addr(struct fpmi_worker_pool_s *wp, const char *addr, const char *port) /* {{{ */
{
	struct addrinfo hints, *servinfo, *p;
	char tmpbuf[INET6_ADDRSTRLEN];
	int status;
	int sock = -1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((status = getaddrinfo(addr, port, &hints, &servinfo)) != 0) {
		zlog(ZLOG_ERROR, "getaddrinfo: %s\n", gai_strerror(status));
		return -1;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		inet_ntop(p->ai_family, fpmi_get_in_addr(p->ai_addr), tmpbuf, INET6_ADDRSTRLEN);
		if (sock < 0) {
			if ((sock = fpmi_sockets_get_listening_socket(wp, p->ai_addr, p->ai_addrlen)) != -1) {
				zlog(ZLOG_DEBUG, "Found address for %s, socket opened on %s", addr, tmpbuf);
			}
		} else {
			zlog(ZLOG_WARNING, "Found multiple addresses for %s, %s ignored", addr, tmpbuf);
		}
	}

	freeaddrinfo(servinfo);

	return sock;
}
/* }}} */

static int fpmi_socket_af_inet_listening_socket(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	char *dup_address = strdup(wp->config->listen_address);
	char *port_str = strrchr(dup_address, ':');
	char *addr = NULL;
	int addr_len;
	int port = 0;
	int sock = -1;

	if (port_str) { /* this is host:port pair */
		*port_str++ = '\0';
		port = atoi(port_str);
		addr = dup_address;

		/* strip brackets from address for getaddrinfo */
		addr_len = strlen(addr);
		if (addr[0] == '[' && addr[addr_len - 1] == ']') {
			addr[addr_len - 1] = '\0';
			addr++;
		}

	} else if (strlen(dup_address) == strspn(dup_address, "0123456789")) { /* this is port */
		port = atoi(dup_address);
		port_str = dup_address;
	}

	if (port == 0) {
		zlog(ZLOG_ERROR, "invalid port value '%s'", port_str);
		return -1;
	}

	if (addr) {
		/* Bind a specific address */
		sock = fpmi_socket_af_inet_socket_by_addr(wp, addr, port_str);
	} else {
		/* Bind ANYADDR
		 *
		 * Try "::" first as that covers IPv6 ANYADDR and mapped IPv4 ANYADDR
		 * silencing warnings since failure is an option
		 *
		 * If that fails (because AF_INET6 is unsupported) retry with 0.0.0.0
		 */
		int old_level = zlog_set_level(ZLOG_ALERT);
		sock = fpmi_socket_af_inet_socket_by_addr(wp, "::", port_str);
		zlog_set_level(old_level);

		if (sock < 0) {
			zlog(ZLOG_NOTICE, "Failed implicitly binding to ::, retrying with 0.0.0.0");
			sock = fpmi_socket_af_inet_socket_by_addr(wp, "0.0.0.0", port_str);
		}
	}

	free(dup_address);

	return sock;
}
/* }}} */

static int fpmi_socket_af_unix_listening_socket(struct fpmi_worker_pool_s *wp) /* {{{ */
{
	struct sockaddr_un sa_un;

	memset(&sa_un, 0, sizeof(sa_un));
	strlcpy(sa_un.sun_path, wp->config->listen_address, sizeof(sa_un.sun_path));
	sa_un.sun_family = AF_UNIX;
	return fpmi_sockets_get_listening_socket(wp, (struct sockaddr *) &sa_un, sizeof(struct sockaddr_un));
}
/* }}} */

int fpmi_sockets_init_main() /* {{{ */
{
	unsigned i, lq_len;
	struct fpmi_worker_pool_s *wp;
	char *inherited = getenv("FPMI_SOCKETS");
	struct listening_socket_s *ls;

	if (0 == fpmi_array_init(&sockets_list, sizeof(struct listening_socket_s), 10)) {
		return -1;
	}

	/* import inherited sockets */
	while (inherited && *inherited) {
		char *comma = strchr(inherited, ',');
		int type, fd_no;
		char *eq;

		if (comma) {
			*comma = '\0';
		}

		eq = strchr(inherited, '=');
		if (eq) {
			*eq = '\0';
			fd_no = atoi(eq + 1);
			type = fpmi_sockets_domain_from_address(inherited);
			zlog(ZLOG_NOTICE, "using inherited socket fd=%d, \"%s\"", fd_no, inherited);
			fpmi_sockets_hash_op(fd_no, 0, inherited, type, FPMI_STORE_SOCKET);
		}

		if (comma) {
			inherited = comma + 1;
		} else {
			inherited = 0;
		}
	}

	/* create all required sockets */
	for (wp = fpmi_worker_all_pools; wp; wp = wp->next) {
		switch (wp->listen_address_domain) {
			case FPMI_AF_INET :
				wp->listening_socket = fpmi_socket_af_inet_listening_socket(wp);
				break;

			case FPMI_AF_UNIX :
				if (0 > fpmi_unix_resolve_socket_premissions(wp)) {
					return -1;
				}
				wp->listening_socket = fpmi_socket_af_unix_listening_socket(wp);
				break;
		}

		if (wp->listening_socket == -1) {
			return -1;
		}

	if (wp->listen_address_domain == FPMI_AF_INET && fpmi_socket_get_listening_queue(wp->listening_socket, NULL, &lq_len) >= 0) {
			fpmi_scoreboard_update(-1, -1, -1, (int)lq_len, -1, -1, 0, FPMI_SCOREBOARD_ACTION_SET, wp->scoreboard);
		}
	}

	/* close unused sockets that was inherited */
	ls = sockets_list.data;

	for (i = 0; i < sockets_list.used; ) {
		if (ls->refcount == 0) {
			close(ls->sock);
			if (ls->type == FPMI_AF_UNIX) {
				unlink(ls->key);
			}
			free(ls->key);
			fpmi_array_item_remove(&sockets_list, i);
		} else {
			++i;
			++ls;
		}
	}

	if (0 > fpmi_cleanup_add(FPMI_CLEANUP_ALL, fpmi_sockets_cleanup, 0)) {
		return -1;
	}
	return 0;
}
/* }}} */

#if HAVE_FPMI_LQ

#ifdef HAVE_LQ_TCP_INFO

#include <netinet/tcp.h>

int fpmi_socket_get_listening_queue(int sock, unsigned *cur_lq, unsigned *max_lq)
{
	struct tcp_info info;
	socklen_t len = sizeof(info);

	if (0 > getsockopt(sock, IPPROTO_TCP, TCP_INFO, &info, &len)) {
		zlog(ZLOG_SYSERROR, "failed to retrieve TCP_INFO for socket");
		return -1;
	}
#if defined(__FreeBSD__) || defined(__NetBSD__)
	if (info.__tcpi_sacked == 0) {
		return -1;
	}

	if (cur_lq) {
		*cur_lq = info.__tcpi_unacked;
	}

	if (max_lq) {
		*max_lq = info.__tcpi_sacked;
	}
#else
	/* kernel >= 2.6.24 return non-zero here, that means operation is supported */
	if (info.tcpi_sacked == 0) {
		return -1;
	}

	if (cur_lq) {
		*cur_lq = info.tcpi_unacked;
	}

	if (max_lq) {
		*max_lq = info.tcpi_sacked;
	}
#endif

	return 0;
}

#endif

#ifdef HAVE_LQ_SO_LISTENQ

int fpmi_socket_get_listening_queue(int sock, unsigned *cur_lq, unsigned *max_lq)
{
	int val;
	socklen_t len = sizeof(val);

	if (cur_lq) {
		if (0 > getsockopt(sock, SOL_SOCKET, SO_LISTENQLEN, &val, &len)) {
			return -1;
		}

		*cur_lq = val;
	}

	if (max_lq) {
		if (0 > getsockopt(sock, SOL_SOCKET, SO_LISTENQLIMIT, &val, &len)) {
			return -1;
		}

		*max_lq = val;
	}

	return 0;
}

#endif

#else

int fpmi_socket_get_listening_queue(int sock, unsigned *cur_lq, unsigned *max_lq)
{
	return -1;
}

#endif

int fpmi_socket_unix_test_connect(struct sockaddr_un *sock, size_t socklen) /* {{{ */
{
	int fd;

	if (!sock || sock->sun_family != AF_UNIX) {
		return -1;
	}

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	if (connect(fd, (struct sockaddr *)sock, socklen) == -1) {
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}
/* }}} */
