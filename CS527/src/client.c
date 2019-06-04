/*
 * Copyright (c) 2018 Yizhou Shan, Liwei Guo, Yajie Geng
 * All rights reserved.
 */

#include <sploit.h>
#include <string.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <pthread.h>
#include <sys/mman.h>
#include <errno.h>
#include <netinet/in.h>

int debug_print = 0;

char server_ip[IP_LEN_MAX];

/*
 * File Transfer Code
 */

/* List of queued file PUT/GET works */
static LIST_HEAD(file_works);
static pthread_spinlock_t file_lock;
static pthread_t file_thread;

/* Queue work into list and wake daemon thread up */
void client_add_file_work(struct file_work *work)
{
	pthread_spin_lock(&file_lock);
	list_add(&work->next, &file_works);
	pthread_spin_unlock(&file_lock);
}

/*
 * Client File PUT
 * We will listen on @work->port
 * Server will try to connect us
 */
static void *do_file_put_to_server(void *arg)
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

/*
 * Client File GET:
 * Server thread is listening on @work->port
 * We should try to connect to this port
 */
static void *do_file_get_from_server(void *arg)
{
	struct file_work *work = arg;
	struct sockaddr_in serv_addr; 
	int ret, n;
	int sockfd, filefd;
	void *recvBuff;

	dp("Connect to %s:%d fname: %s size: %d\n", server_ip, work->port, work->fname, work->size);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Error : Could not create socket");
		return 0;
	} 

	memset(&serv_addr, '0', sizeof(serv_addr)); 
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(work->port);

	/* Connect to the server */
	if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr)<=0) {
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

static void do_file_work(struct file_work *work)
{
	int ret;
	pthread_t tid;

	dump_file_work(work);

	/* Spawn a thread for each file get/put */
	if (work->op == OP_PUT) {
		ret = pthread_create(&tid, NULL, do_file_put_to_server, work);
		BUG_ON(ret);
	} else if (work->op == OP_GET) {
		ret = pthread_create(&tid, NULL, do_file_get_from_server, work);
		BUG_ON(ret);
	} else
		BUG();
}

/*
 * The file daemon thread 
 * It will spawn a new thread for every file transfer request.
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

/*
 * Cached file work. Set before we send message to server,
 * queued and cleared when get reply from server.
 */
static struct file_work *cached_fwork = NULL;

static int prep_get_command(void *cmd)
{
	char *token, *s;
	char fname[FNAME_LEN_MAX];
	int n;
	struct file_work *fwork;

	s = strdup(cmd);

	/* skip GET */
	token = strtok(s, " ");
	if (!token)
		goto usage;

	/* $FILENAME */
	token = strtok(NULL, " ");
	if (!token)
		goto usage;

	/* [get $FILENAME] case */
	n = strlen(token) - 1;
	if (token[n] == '\n')
		token[n] = '\0';

	strncpy(fname, token, FNAME_LEN_MAX);

	/* All good, cached fwork */
	fwork = malloc(sizeof(*fwork));
	if (!fwork)
		return 0;

	strncpy(fwork->fname, fname, FNAME_LEN_MAX);
	fwork->port = -1;
	fwork->size = -1;
	fwork->op = OP_GET;

	if (cached_fwork)
		printf("WARNING: already has cached_work?\n");
	cached_fwork = fwork;

	return 0;

usage:
	printf("Usage: get $FILENAME\n");
	return -EINVAL;
}

/*
 * Check semantic: PUT $FILENAME $SIZE
 * Check if file exist
 * Cache fwork
 */
static int prep_put_command(void *cmd)
{
	char *token, *s;
	char fname[FNAME_LEN_MAX];
	int n, size;
	struct file_work *fwork;

	s = strdup(cmd);

	/* skip PUT */
	token = strtok(s, " ");
	if (!token)
		goto usage;

	/* $FILENAME */
	token = strtok(NULL, " ");
	if (!token)
		goto usage;

	/* [put $FILENAME] case */
	n = strlen(token) - 1;
	if (token[n] == '\n')
		token[n] = '\0';

	strncpy(fname, token, FNAME_LEN_MAX);
	if (!file_exist(fname)) {
		printf("Client: No such file: %s\n", fname);
		return -EINVAL;
	}

	token = strtok(NULL, " ");
	if (!token)
		goto usage;

	size = atoi(token);
	if (size > get_file_size(fname)) {
		printf("Client: size: %d fsize: %zu\n", size, get_file_size(fname));
		return -EINVAL;
	}

	/* All good, cached fwork */
	fwork = malloc(sizeof(*fwork));
	if (!fwork)
		return 0;

	strncpy(fwork->fname, fname, FNAME_LEN_MAX);
	fwork->port = -1;
	fwork->size = size;
	fwork->op = OP_PUT;

	if (cached_fwork)
		printf("WARNING: already has cached_work?\n");
	cached_fwork = fwork;

	return 0;

usage:
	printf("Usage: put $FILENAME $SIZE\n");
	return -EINVAL;
}

static void parse_server_msg(void *msg)
{
	int port;
	size_t size;
	int op = -1;

	if (!strncmp(msg, "get ", 4)) {
		sscanf(msg, "get port: %d size: %zu\n", &port, &size);
		op = OP_GET;
	} else if (!strncmp(msg, "put ", 4)) {
		sscanf(msg, "put port: %d\n", &port);
		op = OP_PUT;
	} else
		return;

	/* Someone is doing something behind our back? ;-) */
	if (!cached_fwork)
		return;

	/* this should be bug */
	BUG_ON(cached_fwork->op != op);

	if (op == OP_GET) {
		cached_fwork->port = port;
		cached_fwork->size = size;
	} else if (op == OP_PUT) {
		cached_fwork->port = port;
	}

	client_add_file_work(cached_fwork);
	cached_fwork = NULL;
}

enum work_mode {
	default_mode,
	auto_mode,
};

int sockfd;
char cmdBuffer[1024];
char recvBuff[1024];

static void die_usage(void)
{
	printf("[Usage] \n"
		"Default Mode: ./client server-ip server-port\n"
		"Automated Mode: ./client server-ip server-port infile outfile\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	int n = 0;
	char paramBuffer[CMD_LEN_MAX];
	char server_port_buff[PORT_LEN_MAX];
	char infile_name[FILE_NAME_MAX];
	char outfile_name[FILE_NAME_MAX];
	int server_port; /* default 31337 */ 
	struct sockaddr_in serv_addr; 
	int ret, work_mode, infile_fd, outfile_fd;

	setbuf(stdout, NULL);

	if (argc != 3 && argc != 5)
		die_usage();

	/* Choose the work mode based on input */
	if (argc == 3) {
		work_mode = default_mode;
	} else if (argc == 5) {
		work_mode = auto_mode;
		strncpy(infile_name, argv[3], MAX(strlen(argv[3]) + 1, FILE_NAME_MAX)); 
		strncpy(outfile_name, argv[4], MAX(strlen(argv[4]) + 1,  FILE_NAME_MAX));
	} else {
		printf("Error: Invalid mode\n");
		return 0;
	}

	/* Either default mode or auto mode, copy server ip and port  */
	strncpy(server_ip, argv[1], MAX(strlen(argv[1]) + 1, IP_LEN_MAX)); 
	strncpy(server_port_buff, argv[2], MAX(strlen(argv[2]) + 1, PORT_LEN_MAX));
	server_port = atoi(server_port_buff);

	printf("Connecting to ");
	printf(server_ip);
	printf(" : %d\n", server_port);

	/* init server addr, port, buffer  */
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Error : Could not create socket \n");
		return 1;
	} 
	memset(&serv_addr, '0', sizeof(serv_addr)); 
	memset(recvBuff, '\0',sizeof(recvBuff));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(server_port); 

	/* Connect to the server */
	if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr)<=0) {
		perror("inet_pton error occured\n");
		return 1;
	} 
	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("Error: Connect Failed \n");
		return 1;
	} 

	if (work_mode == auto_mode) {
		infile_fd = open(infile_name, O_RDONLY);
		if (!infile_fd) {
			printf("Error: no valid input file under auto mode, abort!\n");
			return 0;
		}
		outfile_fd = open(outfile_name, O_CREAT | O_RDWR, S_IREAD | S_IWRITE);
		dup2(infile_fd, STDIN_FILENO);
		dup2(outfile_fd, STDOUT_FILENO);
		close(outfile_fd);
	}

	/* Create background file threads */
	ret = init_file_thread();
	if (ret)
		return ret;

	/* Our main loop */
	while (1) {
		memset(cmdBuffer, '\0', sizeof(cmdBuffer));
		memset(recvBuff, '\0', sizeof(recvBuff));

		/* Meeting the end of file in auto mode */	
		if (!fgets(cmdBuffer, sizeof(cmdBuffer) + 100, stdin)) {
			__fpurge(stdout);
			return 0;
		}

		BUG_ON(!cmdBuffer);

		/*
		 * Things we can do before sending commands to server
		 * - exit: exit this client program
		 * - put: check if file exist, size is within limit; Save fwork;
		 * - get: save fwork
		 */
		if (!strncmp("exit", cmdBuffer, 4)) {
			printf("Bye!\n");
			send(sockfd, cmdBuffer, strlen(cmdBuffer) + 1, 0);
			exit(0);
		} else if (!strncmp("put", cmdBuffer, 3)) {
			if (prep_put_command(cmdBuffer))
				continue;
		} else if (!strncmp("get", cmdBuffer, 3)) {
			if (prep_get_command(cmdBuffer))
				continue;
		}

		n = send(sockfd, cmdBuffer, strlen(cmdBuffer) + 1, 0);
		n = recv(sockfd, recvBuff, sizeof(recvBuff)-1, 0);
		if (n <= 0)
			continue;

		/*
		 * Things we should do regarding the received messages:
		 * - get
		 * - put
		 */
		parse_server_msg(recvBuff);
		printf("%s", recvBuff);
		fflush(stdout);
	}
	return 0;
}
