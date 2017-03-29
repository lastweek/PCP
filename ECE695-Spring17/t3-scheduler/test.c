#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>

#define NR_SCHED_SETLIMIT	332
#define SCHED_MYCFS		7

int sched_setlimit(pid_t pid, int limit)
{
	syscall(NR_SCHED_SETLIMIT, pid, limit);
}


int main(void)
{
	struct sched_param param = { 0 };
	pid_t pid;

	printf("Old Policy: %d\n", sched_getscheduler(0));
	sched_setscheduler(0, SCHED_MYCFS, &param);
	printf("New Policy: %d\n", sched_getscheduler(0));

	sched_setlimit(0, 50);

	if ((pid = fork()) != 0) {
		printf("pid: %d, policy: %d\n", getpid(), sched_getscheduler(0));
	} else
		printf("pid: %d, policy: %d\n", getpid(), sched_getscheduler(0));

	return 0;
}
