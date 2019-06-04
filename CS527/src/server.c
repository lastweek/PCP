/*
 * Copyright (c) 2018 Yizhou Shan, Liwei Guo, Yajie Geng
 * All rights reserved.
 */

#include <sploit.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <pthread.h>

int debug_print = 0;

/*
 * Session Management
 */

static int session_time_s = 60*60;

static LIST_HEAD(session_list);
static int nr_sessions;
pthread_spinlock_t session_lock;

static struct session *alloc_session(struct User *user, char *ip, int port)
{
	struct session *s;

	BUG_ON(!user);

	s = malloc(sizeof(*s));
	if (s) {
		strncpy(s->ip, ip, IP_LEN_MAX);
		s->port = port;
		s->user = user;
		INIT_LIST_HEAD(&s->next);
		s->t_logging = time(NULL);
		s->t_expire = s->t_logging + session_time_s;

		/* Still passwd to be valid */
		s->flags = SESSION_WAIT_PASSED;
	}
	return s;
}

static void add_session(struct session *s)
{
	BUG_ON(!s || !s->user || !s->t_expire);

	pthread_spin_lock(&session_lock);
	list_add(&s->next, &session_list);
	pthread_spin_unlock(&session_lock);
}

static struct session *find_session(char *ip, int port)
{
	struct session *s;

	pthread_spin_lock(&session_lock);
	list_for_each_entry(s, &session_list, next) {
		if (!strncmp(s->ip, ip, IP_LEN_MAX) &&
		    s->port == port)
			/* found */
			goto out;
	}
	s = NULL;
out:
	pthread_spin_unlock(&session_lock);
	return s;
}

static void remove_session(char *ip, int port)
{
	struct session *s;

	pthread_spin_lock(&session_lock);
	BUG_ON(list_empty(&session_list));

	list_for_each_entry(s, &session_list, next) {
		if (!strncmp(s->ip, ip, IP_LEN_MAX) &&
		    s->port == port) {
			list_del(&s->next);
			free(s);
			break;
		}
	}
	pthread_spin_unlock(&session_lock);
}

/* Check if a given client has a valid logged session */
static bool valid_session(char *ip, int port)
{
	struct session *s;
	bool valid = false;

	pthread_spin_lock(&session_lock);
	if (list_empty(&session_list))
		goto unlock;

	list_for_each_entry(s, &session_list, next) {
		if (!strncmp(s->ip, ip, IP_LEN_MAX) &&
		    s->port == port &&
		    s->flags != SESSION_WAIT_PASSED) {
			valid = true;
			break;
		}
	}

unlock:
	pthread_spin_unlock(&session_lock);
	return valid;
}

/*
 * Built-in Commands
 */

struct Command cmdlist[] = {
{
	.cname = "login",
	.handler = handle_login,
},
{
	.cname = "pass",
	.handler = handle_pass,
},
{
	.cname = "ping",
	.handler = handle_ping,
},
{
	.cname = "logout",
	.handler = handle_logout,
},
{
	.cname = "exit",
	.handler = handle_exit,
},
{
	.cname = "get",
	.handler = handle_get,
},
{
	.cname = "put",
	.handler = handle_put,
},
{
	.cname = "cd",
	.handler = handle_common,
},
{
	.cname = "w",
	.handler = handle_w,
},
{
	.cname = "whoami",
	.handler = handle_whoami,
},
};

static const char err_not_found[] = "Error: command not found\n";

/*
 * The fork and execve way
 * used to run all other unix commands
 */
int handle_common(struct Command *cmd, int sock, char *ip, int port)
{
	pid_t pid;

	printf("%s %s\n", cmd->cname, cmd->params);
	pid = fork();
	if (pid == 0) {
		/* Both stdout and stderr are forwarded */
		dup2(sock, STDOUT_FILENO);
		dup2(sock, STDERR_FILENO);

		/* CD is a weird command */
		if (!strcmp(cmd->cname, "cd")) {
			if(chdir(cmd->params) < 0)  {
				exit(-1);
			}
		} else if (execlp(cmd->cname, cmd->cname, cmd->params, NULL)) {
			write(sock, err_not_found, strlen(err_not_found));
			exit(-1);
		}
	} else
		wait(NULL);

	return 0;
}

/*
 * cached session which is waiting for the pass command
 */
static struct session *cached_session;

int handle_login(struct Command *cmd, int sock, char *ip, int port)
{
	struct User *user;
	struct session *s;

	if (!cmd->params) {
		write(sock, "\n", 1);
		return 0;
	}

	user = find_user_by_name(cmd->params);
	if (!user) {
		const char invalid_user[] = "Not a valid user name.\n";
		write(sock, invalid_user, strlen(invalid_user));
		return 0;
	}

	s = alloc_session(user, ip, port);
	BUG_ON(!s);
	cached_session = s;

	return 0;
}

static const char welcome_login[] = "Welcome!\n";

int handle_pass(struct Command *cmd, int sock, char *ip, int port)
{
	if (!cached_session || !cmd->params) {
		write(sock, "\n", 1);
		return 0;
	}

	if (strncmp(cmd->params, cached_session->user->passwd, USER_NAME_MAX_LEN)) {
		const char invalid_passwd[] = "Invalid passwd.\n";
		write(sock, invalid_passwd, strlen(invalid_passwd));
		return 0;
	}

	cached_session->flags = 0;
	add_session(cached_session);
	cached_session = NULL;

	write(sock, welcome_login, strlen(welcome_login));

	return 0;
}

/* Remove this client's session */
int handle_logout(struct Command *cmd, int sock, char *ip, int port)
{
	const char logging_out[] = "Logged out...\n";

	remove_session(ip, port);

	write(sock, logging_out, strlen(logging_out));
	return 0;
}

/* List all loggin users */
int handle_w(struct Command *cmd, int sock, char *ip, int port)
{
	struct session *s;
	char uname[USER_NAME_MAX_LEN];

	pthread_spin_lock(&session_lock);
	list_for_each_entry(s, &session_list, next) {
		sprintf(uname, "%s\n", s->user->uname);
		write(sock, uname, strlen(uname));
	}
	pthread_spin_unlock(&session_lock);
	return 0;
}

int handle_whoami(struct Command *cmd, int sock, char *ip, int port)
{
	struct session *s;
	char uname[USER_NAME_MAX_LEN];

	s = find_session(ip, port);

	/* Must exist */
	BUG_ON(!s);

	sprintf(uname, "%s\n", s->user->uname);
	write(sock, uname, strlen(uname));
	return 0;
}

int handle_exit(struct Command *cmd, int sock, char *ip, int port)
{
	printf("%s(): %s\n", __func__, cmd->originalString);
	return 0;
}

static const char ping_usage[] = "Usage: ping $HOST\n";
int handle_ping(struct Command *cmd, int sock, char *ip, int port)
{
	char *host, *token;
	char ping_cmd[CMD_LEN_MAX];
	pid_t pid;

	if (!cmd->params)
		goto out;

	token = strtok(cmd->params, " ");
	if (!token)
		goto out;
	else
		host = token;

	token = strtok(NULL, " ");
	if (token)
		goto out;

	sprintf(ping_cmd, "-c 1 %s", host);

	pid = fork();
	if (pid == 0) {
		dup2(sock, STDOUT_FILENO);
		dup2(sock, STDERR_FILENO);

		if (execlp("ping", "ping", host, "-c 1", NULL)) {
			write(sock, err_not_found, strlen(err_not_found));
			exit(-1);
		}
	} else
		wait(NULL);

	return 0;

out:
	write(sock, ping_usage, strlen(ping_usage));
	return 0;
}

static int get_file_port = 31338;
static int put_file_port = 31339;

static int assign_get_port(void)
{
	return get_file_port;
}

static int assign_put_port(void)
{
	return put_file_port;
}

static const char get_usage[] = "Usage: get $FILENAME\n";
static const char get_no_file[] = "GET: No such file.\n";
int handle_get(struct Command *cmd, int sock, char *ip, int port)
{
	char *fname, *token;
	struct file_work *fwork;
	char msg[CMD_LEN_MAX];
	struct session *s;

	if (!cmd->params)
		goto out_usage;

	/* get filename */
	token = strtok(cmd->params, " ");
	BUG_ON(!token);
	fname = token;

	/* should not have anything else */
	token = strtok(NULL, " ");
	if (token)
		goto out_usage;

	if (!file_exist(fname)) {
		write(sock, get_no_file, strlen(get_no_file));
		return 0;
	}

	/* Queue file get work */
	fwork = malloc(sizeof(*fwork));
	BUG_ON(!fwork);
	fwork->op = OP_GET;
	strncpy(fwork->fname, fname, FNAME_LEN_MAX);
	fwork->port = assign_get_port();
	fwork->size = get_file_size(fname);

	s = find_session(ip, port);
	BUG_ON(!s);
	fwork->session = s;
	server_add_file_work(fwork);

	/*
	 * Tell client
	 * client will parse the string and spawn a new thread
	 */
	sprintf(msg, "get port: %d size: %d\n", fwork->port, fwork->size);
	write(sock, msg, sizeof(msg));

	return 0;

out_usage:
	write(sock, get_usage, strlen(get_usage));
	return 0;
}

static const char put_usage[] = "Usage: put $FILENAME $SIZE\n";
int handle_put(struct Command *cmd, int sock, char *ip, int port)
{
	char *token, *fname;
	unsigned int fsize;
	struct file_work *fwork;
	char msg[CMD_LEN_MAX];
	struct session *s;

	if (!cmd->params)
		goto err;

	/* get filename */
	token = strtok(cmd->params, " ");
	if (!token)
		goto err;
	fname = token;

	/* get file size */
	token = strtok(NULL, " ");
	if (!token)
		goto err;
	fsize = atoi(token);

	/* should not have anything else */
	token = strtok(NULL, " ");
	if (token)
		goto err;

	/* Queue file put work */
	fwork = malloc(sizeof(*fwork));
	BUG_ON(!fwork);
	fwork->op = OP_PUT;
	strncpy(fwork->fname, fname, FNAME_LEN_MAX);
	fwork->port = assign_put_port();
	fwork->size = fsize;

	s = find_session(ip, port);
	BUG_ON(!s);
	fwork->session = s;
	server_add_file_work(fwork);

	/*
	 * Tell client
	 * client will spawn a new thread
	 */
	sprintf(msg, "put port: %d\n", fwork->port);
	write(sock, msg, sizeof(msg));

	return 0;
err:
	write(sock, put_usage, strlen(put_usage));
	return 0;
}

static LIST_HEAD(file_works);
static pthread_spinlock_t file_lock;
static pthread_t file_thread;

/* Queue work into list and wake daemon thread up */
void server_add_file_work(struct file_work *work)
{
	pthread_spin_lock(&file_lock);
	list_add(&work->next, &file_works);
	pthread_spin_unlock(&file_lock);
}

/*
 * Server File PUT
 * Client thread is listening on @work->port
 * We should try to connect to this port
 */
static void *do_file_put_from_client(void *arg)
{
	struct file_work *work = arg;
	struct session *session = work->session;
	struct sockaddr_in serv_addr; 
	int ret, n;
	int sockfd, filefd;
	void *recvBuff;
	const char *client_ip = session->ip;

	dp("Connect to %s:%d fname: %s size: %d\n", client_ip, work->port, work->fname, work->size);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Error : Could not create socket");
		return 0;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(work->port);

	/* Connect to the client */
	if (inet_pton(AF_INET, client_ip, &serv_addr.sin_addr)<=0) {
		perror("inet_pton error occured");
		return 0;
	} 

	while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		sleep(1);
	}

	recvBuff = malloc(work->size);
	BUG_ON(!recvBuff);
	n = recv(sockfd, recvBuff, work->size, 0);
	BUG_ON(n != work->size);

	/*
	 * Append ~ at the end
	 */
	n = strlen(work->fname);
	work->fname[n] = '~';
	work->fname[n+1] = '\0';

	/*
	 * Do not override
	 * Append..
	 */
	filefd = open(work->fname, O_CREAT | O_RDWR | O_APPEND, 0666);
	write(filefd, recvBuff, work->size);

	close(filefd);
	close(sockfd);
}

/*
 * Server File GET:
 * We will listen on @work->port
 * and waiting for client's connection
 *
 * TODO: We really do not have too much elegant error handling here.
 * If anything went wrong, we should let remote know. Well.
 */
static void *do_file_get_to_client(void *arg)
{
	struct file_work *work = arg;
	struct sockaddr_in serv_addr; 
	int listenfd, connfd, filefd;
	void *file_base;
	size_t n;

	dp("Listen on port %d fname: %s size: %d\n", work->port, work->fname, work->size);

	/* open and map file */
	filefd = open(work->fname, O_RDWR);
	if (filefd < 0) {
		perror("Fail to open file");
		return 0;
	}

	file_base = mmap(0, work->size, PROT_READ|PROT_WRITE, MAP_SHARED, filefd, 0);
	if (file_base == MAP_FAILED) {
		perror("Fail to mmap");
		return 0;
	}

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		perror("Fail to creat socket");
		return 0;
	}

	memset(&serv_addr, '0', sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(work->port);

	/* listen */
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
		perror("fail to bind");
		return 0;
	}

	if (listen(listenfd, 10)) {
		perror("fail to listen");
		return 0;
	}

	connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); 
	if (connfd < 0) {
		perror("fail to accept");
		return 0;
	}

	n = write(connfd, file_base, work->size);
	BUG_ON(n != work->size);

	close(filefd);
	close(connfd);
}

static void do_file_work(struct file_work *work)
{
	int ret;
	pthread_t tid;

	dump_file_work(work);

	/* Spawn a thread for each file get/put */
	if (work->op == OP_PUT) {
		ret = pthread_create(&tid, NULL, do_file_put_from_client, work);
		BUG_ON(ret);
	} else if (work->op == OP_GET) {
		ret = pthread_create(&tid, NULL, do_file_get_to_client, work);
		BUG_ON(ret);
	} else
		BUG();
}

/*
 * This is the top file transfer daemon. It will look into the work list,
 * and spawn a new thread for each file transfer request.
 *
 * TODO: sleep and wakeup. Nothing like wake_up_process()?
 */
static void *file_tran_thread(void *arg)
{
	struct file_work *work;

	while (1) {
		pthread_spin_lock(&file_lock);
		while (!list_empty(&file_works)) {
			work = list_entry(file_works.next, struct file_work, next);
			list_del_init(&work->next);

			pthread_spin_unlock(&file_lock);
			do_file_work(work);
			pthread_spin_lock(&file_lock);
		}
		pthread_spin_unlock(&file_lock);
	}
}

static int init_file_thread(void)
{
	int ret;

	pthread_spin_init(&file_lock, PTHREAD_PROCESS_PRIVATE);

	ret = pthread_create(&file_thread, NULL, file_tran_thread, NULL);
	if (ret) {
		perror("Fail to create thread");
		return ret;
	}
	return 0;
}

static struct Command *find_command_by_cname(char *cname)
{
	int i;
	struct Command *c;

	for (i = 0; i < ARRAY_SIZE(cmdlist); i++) {
		c = cmdlist + i;
		if (!strncmp(cname, c->cname, CMD_LEN_MAX))
			return c;
	}
	return NULL;
}

enum prompt_result {
	pr_empty,	/* empty string received */
	pr_valid,	/* need session validation */

	pr_login,	/* below are built-in special commands*/
	pr_pass,
	pr_ping,
};

static int find_command(char *cmd_str, struct Command *cmd)
{
	char *cname, *param;
	struct Command *systemCmd;
	int ret;

	cname = strtok(cmd_str, " ");
	if (!cname)
		return pr_empty;

	/*
	 * Find our built-in system commands
	 * Also report the cmd type
	 */
	strncpy(cmd->cname, cname, CMD_LEN_MAX);
	systemCmd = find_command_by_cname(cname);
	if (systemCmd) {
		cmd->handler = systemCmd->handler;
	
		if (!strncmp("login", cmd->cname, 5))
			ret = pr_login;
		else if (!strncmp("pass", cmd->cname, 4))
			ret = pr_pass;
		else if (!strncmp("ping", cmd->cname, 4))
			ret = pr_ping;
		else
			/* which require login */
			ret = pr_valid;
	} else {
		cmd->handler = handle_common;
		ret = pr_valid;
	}

	param = strtok(NULL, "");
	if (param)
		cmd->params = param;
	else
		cmd->params = NULL;

	return ret;
}

static const char not_logged_prompt[] =
"\n"
"**** You need to login first!\n"
"****                         \n"
"****     login $USERNAME     \n"
"****     pass $PASSWORD      \n"
"****                         \n";

static char PS1[] = "$ ";

static void run_command(char *command, int sock, char *ip, int port)
{
	struct Command cmd;
	size_t len;
	char *s;
	int ret, good_session;

	cmd.originalString = command;

	s = strndup(command, CMD_LEN_MAX);
	BUG_ON(!s);

	/* remove the ending new line */
	len = strlen(s) - 1;
	if (s[len] == '\n') {
		s[len] = '\0';
		command[len] = '\0';
	}

	ret = find_command(s, &cmd);
	switch (ret) {
	case pr_login:
	case pr_pass:
	case pr_ping:
		cmd.handler(&cmd, sock, ip, port);
		goto fall;
	case pr_valid:
		if (valid_session(ip, port))
			cmd.handler(&cmd, sock, ip, port);
		else
			write(sock, not_logged_prompt, strlen(not_logged_prompt));
		/* Fall throught */
	case pr_empty:
fall:
		write(sock, PS1, strlen(PS1));
		break;
	default:
		BUG();
	}
}

int main(void)
{
	int listenfd = 0, connfd = 0, cmd_size = 0;
	char cmdBuffer[CMD_LEN_MAX];
	struct sockaddr_in serv_addr; 
	char sendBuff[1025];
	int ret;
	struct sockaddr_in client_addr;
	int client_len = sizeof(client_addr);
	char *client_ip;
	int client_port;

	pthread_spin_init(&session_lock, PTHREAD_PROCESS_PRIVATE);

	ret = parse_sploit("./sploit.conf");
	if (ret)
		return -EINVAL;

	ret = init_file_thread();
	if (ret)
		return ret;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));
	memset(sendBuff, '0', sizeof(sendBuff)); 
	memset(cmdBuffer, '0', sizeof(cmdBuffer));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Port is written in config file */
	serv_addr.sin_port = htons(server_listen_port); 

	bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

	listen(listenfd, 10);
	connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);

	client_ip = inet_ntoa(client_addr.sin_addr);
	client_port = ntohs(client_addr.sin_port);
	printf("Incoming connection from %s:%d\n", client_ip, client_port);

	while (1) {
		memset(cmdBuffer, '0', sizeof(cmdBuffer));
		cmd_size =  recv(connfd, cmdBuffer, CMD_LEN_MAX - 1, 0);
		if (cmd_size < 0)
			break;

		run_command(cmdBuffer, connfd, client_ip, client_port);
	}

	close(connfd);
	printf("Server: closed!\n");
	return 0;
}
