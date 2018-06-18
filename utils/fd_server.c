#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "fd_server.h"

struct nmd_entry {
	struct nm_desc *nmd;
	uint8_t is_in_use;
	uint8_t is_open;
};

#define SOCKET_NAME "/tmp/my_unix_socket"
#define MAX_OPEN_IF 128
struct nmd_entry entries[MAX_OPEN_IF];
int num_entries = 0;

static void
print_request(struct fd_request *req)
{

	syslog(LOG_NOTICE, "d action: %s, if_name: %s\n",
	       req->action == FD_GET
		       ? "FD_GET"
		       : req->action == FD_RELEASE
				 ? "FD_RELEASE"
				 : req->action == FD_CLOSE ? "FD_CLOSE"
							   : "FD_STOP",
	       req->if_name);
}

struct nmd_entry *
search_des(const char *if_name)
{
	int i;

	for (i = 0; i < num_entries; ++i) {
		struct nmd_entry *entry = &entries[i];
		struct nm_desc *nmd     = entry->nmd;

		if (entry->is_open == 0) {
			continue;
		}

		if (strcmp(nmd->req.nr_name, if_name) == 0) {
			return entry;
		}
	}

	return NULL;
}

struct nmd_entry *
get_free_des(int *ret)
{

	if (num_entries == MAX_OPEN_IF) {
		*ret = -1;
		return NULL;
	}

	*ret = 0;
	return &entries[num_entries++];
}

int
get_fd(const char *if_name, struct fd_response *res)
{
	struct nmd_entry *entry;
	int ret;

	entry = search_des(if_name);
	if (entry != NULL) {
		if (entry->is_in_use == 1) {
			syslog(LOG_NOTICE, "if_name %s is in use\n", if_name);
			res->result = EBUSY;
			return -1;
		}
		memcpy(&res->req, &entry->nmd->req, sizeof(entry->nmd->req));
		return entry->nmd->fd;
	}

	entry = get_free_des(&ret);
	if (ret == -1) {
		syslog(LOG_NOTICE, "Out of memory\n");
		res->result = ENOMEM;
		return -1;
	}

	entry->nmd = nm_open(if_name, NULL, 0, NULL);
	if (entry->nmd == NULL) {
		syslog(LOG_NOTICE, "Failed to nm_open(%s) with error %d\n",
		       if_name, errno);
		res->result = errno;
		return -1;
	}

	memcpy(&res->req, &entry->nmd->req, sizeof(entry->nmd->req));
	entry->is_in_use = 1;
	entry->is_open   = 1;
	return entry->nmd->fd;
}

void
release_fd(const char *if_name, struct fd_response *res)
{
	struct nmd_entry *entry;

	entry = search_des(if_name);
	if (entry == NULL) {
		syslog(LOG_NOTICE, "if_name %s isn't open\n", if_name);
		res->result = ENOENT;
		return;
	}

	entry->is_in_use = 0;
}

void
close_fd(const char *if_name, struct fd_response *res)
{
	struct nmd_entry *entry;
	int ret;

	if (if_name == NULL || strnlen(if_name, NETMAP_REQ_IFNAMSIZ) == 0) {
		res->result = EINVAL;
		return;
	}

	entry = search_des(if_name);
	if (entry == NULL) {
		res->result = ENOENT;
		syslog(LOG_NOTICE, "if_name %s hasn't been opened\n", if_name);
		return;
	}

	ret	 = nm_close(entry->nmd);
	res->result = ret;
	if (ret != 0) {
		syslog(LOG_NOTICE, "error while close interface %s\n", if_name);
		return;
	}
	entry->is_in_use = 0;
	entry->is_open   = 0;
}

int
send_fd(int socket, int fd, void *buf, size_t buf_size)
{
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} ancillary;
	struct cmsghdr *cmsg;
	struct iovec iov[1];
	struct msghdr msg;

	iov[0].iov_base = buf;
	iov[0].iov_len  = buf_size;

	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov    = iov;
	msg.msg_iovlen = 1;

	if (fd >= 0) {
		/* We need the ancillary data only when we're sending a file
		 * descriptor, and a file descriptor cannot be negative.
		 */
		msg.msg_control    = ancillary.buf;
		msg.msg_controllen = sizeof(ancillary.buf);

		cmsg			= CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level	= SOL_SOCKET;
		cmsg->cmsg_type		= SCM_RIGHTS;
		cmsg->cmsg_len		= CMSG_LEN(sizeof(int));
		*(int *)CMSG_DATA(cmsg) = fd;
	}

	return sendmsg(socket, &msg, 0);
}

int
handle_request(int socket)
{
	struct fd_response res;
	struct fd_request req;
	int amount;
	int fd = -1;

	memset(&res, 0, sizeof(res));
	memset(&req, 0, sizeof(req));

	amount = recv(socket, &req, sizeof(struct fd_request), 0);
	if (amount == -1) {
		syslog(LOG_NOTICE, "error during recv()");
		return -1;
	}

	print_request(&req);
	memset(&res, 0, sizeof(res));
	switch (req.action) {
	case FD_GET:
		fd = get_fd(req.if_name, &res);
		break;
	case FD_RELEASE:
		release_fd(req.if_name, &res);
		return 0;
	case FD_CLOSE:
		close_fd(req.if_name, &res);
		return 0;
	case FD_STOP:
		syslog(LOG_NOTICE, "shutting down");
		exit(EXIT_SUCCESS);
		break;
	default:
		res.result = EOPNOTSUPP;
	}

	return send_fd(socket, fd, &res, sizeof(struct fd_response));
}

void
main_loop(void)
{
	struct sockaddr_un name;
	int socket_fd;
	int ret;

	socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (socket_fd == -1) {
		syslog(LOG_NOTICE, "error during socket()");
		exit(EXIT_FAILURE);
	}

	unlink(SOCKET_NAME);
	memset(&name, 0, sizeof(struct sockaddr_un));
	name.sun_family = AF_UNIX;
	strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);
	ret = bind(socket_fd, (const struct sockaddr *)&name,
		   sizeof(struct sockaddr_un));
	if (ret == -1) {
		syslog(LOG_NOTICE, "error during bind()");
		exit(EXIT_FAILURE);
	}

	ret = listen(socket_fd, 2);
	if (ret == -1) {
		syslog(LOG_NOTICE, "error during listen()");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		int conn_fd;
		int ret;

		conn_fd = accept(socket_fd, NULL, NULL);
		if (conn_fd == -1) {
			syslog(LOG_NOTICE,
			       "error during accept(), shutting down");
			exit(EXIT_FAILURE);
		}

		syslog(LOG_NOTICE, "handling a request");
		ret = handle_request(conn_fd);
		(void)ret;
		close(conn_fd);
	}
}

void
daemonize(void)
{
	pid_t pid;
	int i;

	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	if (setsid() < 0) {
		exit(EXIT_FAILURE);
	}

	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	umask(0);
	if (chdir("/") == -1) {
		perror("chdir()");
		exit(EXIT_FAILURE);
	}
	for (i = sysconf(_SC_OPEN_MAX); i >= 0; i--) {
		close(i);
	}

	openlog("nm_fd_server", LOG_PID, LOG_DAEMON);
}

int
main()
{

	daemonize();
	main_loop();
	return 0;
}