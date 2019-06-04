#ifndef SPLOIT_H
#define SPLOIT_H

#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "list.h"

extern int debug_print;
#define	dp(fmt, ...)							\
do {									\
	if (debug_print)						\
		printf("%s(): " fmt "\n", __func__, __VA_ARGS__);	\
} while (0)

#define BUG_ON(cond)	assert(!(cond))
#define BUG()		BUG_ON(1)

#define CMD_LEN_MAX		256
#define IP_LEN_MAX		16  /* 255.255.255.255 */
#define PORT_LEN_MAX		16  /* 65535 */
#define FILE_NAME_MAX		256

#define USER_NAME_MAX_LEN	256
#define USER_PASSWD_MAX_LEN	256

#define MAX(a,b)	((a) > (b) ? (a) : (b))

struct User {
	char uname[USER_NAME_MAX_LEN];
	char passwd[USER_PASSWD_MAX_LEN];

	bool isLoggedIn;

	struct list_head next;
};

#define SESSION_WAIT_PASSED	0x1

struct session {
	struct User	*user;
	char		ip[IP_LEN_MAX];
	int		port;
	int		flags;
	time_t		t_logging;
	time_t		t_expire;

	struct list_head next;
};

#define STRING_MAX_LEN 256

struct alias {
	char alias[STRING_MAX_LEN];	/* Maybe used by client */
	char cname[STRING_MAX_LEN];	/* the real exec name */
	char parameters[STRING_MAX_LEN];

	struct list_head next;
};

struct Command;

typedef	int(*cmd_call_ptr_t)(struct Command *, int, char *, int);

struct Command {
	/*
	 * The real command name used to exectue, after alias
	 * excluding the parameters. Used to find the the cmd.
	 */
	char cname[CMD_LEN_MAX];
	cmd_call_ptr_t	handler;

	/* The original string passed by Client */
	char *originalString;
	char *params;
};

/* server.c */
extern struct Command cmdlist[];

int handle_pass(struct Command *cmd, int sock, char *ip, int port);
int handle_login(struct Command *cmd, int sock, char *ip, int port);
int handle_ping(struct Command *cmd, int sock, char *ip, int port);
int handle_exit(struct Command *cmd, int sock, char *ip, int port);
int handle_logout(struct Command *cmd, int sock, char *ip, int port);
int handle_put(struct Command *cmd, int sock, char *ip, int port);
int handle_get(struct Command *cmd, int sock, char *ip, int port);
int handle_whoami(struct Command *cmd, int sock, char *ip, int port);
int handle_w(struct Command *cmd, int sock, char *ip, int port);
int handle_common(struct Command *cmd, int sock, char *ip, int port);

struct User *find_user_by_name(char *uname);

/* server_conf.c */
int expandCommandAlias(char *original, char *buf, int max_len);
bool checkUser(struct User *user);
int parse_sploit(const char *filename);

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* OP from server to client */
#define OP_PROMPT_PASSED	0x57AC6E9D
#define OP_LOGIN_SUCCEED	0x57AC6E9E
#define OP_GET			0x57AC6E9F
#define OP_PUT			0x57AC6EA0

/* file transfer */
#define	FNAME_LEN_MAX	CMD_LEN_MAX

/*
 * Just one big structure used by all parties
 * and all commands.
 */
struct msg {
	int op;
	int port;
	size_t size;
	unsigned char fname[FNAME_LEN_MAX];
};

struct file_work {
	/*
	 * reuse above OP_GET and OP_PUT
	 */
	int op;
	unsigned char fname[FNAME_LEN_MAX];
	int port;
	size_t size;

	struct session *session;

	struct list_head next;
};

void server_add_file_work(struct file_work *work);
void client_add_file_work(struct file_work *work);

static inline void dump_file_work(struct file_work *work)
{
	dp("%s name: %s port: %d size: %zu\n",
		work->op == OP_PUT ? "PUT" : "GET",
		work->fname, work->port, work->size);
}

static inline bool file_exist(char *fname)
{
	if (access(fname, F_OK) != -1)
		return true;
	else
		return false;
}

static inline size_t get_file_size(char *fname)
{
	struct stat st;

	if (stat(fname, &st) == 0)
		return st.st_size;
	return -1;
}

extern int server_listen_port;

#endif /* SPLOIT_H */
