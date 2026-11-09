/* caminfo: read CAM_A state + camraw irqstats, no register writes.
 * usage: caminfo [--wait ms] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#define CAM_BASE 0x1a030000UL
static volatile uint32_t *mmio;
static inline uint32_t R(unsigned long o){ return mmio[(CAM_BASE-0x10000000+o)/4]; }
static void show(const char *tag)
{
	printf("%s TG=%08x CTL_EN=%08x INT_EN=%08x INT6=%08x INT7=%08x IMGO_ERR=%08x CQ=%08x THR0=%08x BASE=%08x SIZE=%08x IMGO_BASE=%08x XSIZE=%08x\\n",
	       tag, R(0x1f3c), R(0x4), R(0x100), R(0x154), R(0x158), R(0x4068),
	       R(0x200), R(0x210), R(0x214), R(0x218), R(0x4820), R(0x482c));
}
int main(int argc, char **argv)
{
	int wait_ms = 0;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--wait") && i + 1 < argc)
			wait_ms = atoi(argv[++i]);
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }
	mmio = mmap(NULL, 0x20000000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x10000000);
	if (mmio == MAP_FAILED) { perror("mmap"); return 1; }
	show("info");
	if (wait_ms > 0) {
		unsigned int tg0 = R(0x1f3c);
		usleep(wait_ms * 1000);
		unsigned int tg1 = R(0x1f3c);
		printf("TG advanced: %08x -> %08x (%s)\\n", tg0, tg1,
		       tg0 != tg1 ? "YES" : "NO");
	}
	return 0;
}
