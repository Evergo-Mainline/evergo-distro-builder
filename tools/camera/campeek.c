/* campeek: read 32-bit registers from physical addresses via /dev/mem mmap */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s addr [count] [stride]\n", argv[0]); return 1; }
	unsigned long addr = strtoul(argv[1], 0, 0);
	int count = argc > 2 ? atoi(argv[2]) : 1;
	unsigned long stride = argc > 3 ? strtoul(argv[3], 0, 0) : 4;
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }
	void *map = mmap(NULL, 0x30000000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) { perror("mmap"); return 1; }
	for (int i = 0; i < count; i++) {
		unsigned long a = addr + i * stride;
		uint32_t v = *(volatile uint32_t *)((char *)map + a);
		printf("%08lx = %08x\n", a, v);
	}
	return 0;
}
