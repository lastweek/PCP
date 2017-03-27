#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#define NR_SCHED_SETLIMIT	332

int sched_setlimit(pid_t pid, int limit)
{
	syscall(NR_SCHED_SETLIMIT, pid, limit);
}

int main(void)
{
	sched_setlimit(0, 100);

	return 0;
}
