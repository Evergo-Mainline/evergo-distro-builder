/* campoke: write 32-bit registers via /dev/mem mmap */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s addr value [count] [stride]\n", argv[0]); return 1; }
	unsigned long addr = strtoul(argv[1], 0, 0);
	uint32_t val = strtoul(argv[2], 0, 0);
	int count = argc > 3 ? atoi(argv[3]) : 1;
	unsigned long stride = argc > 4 ? strtoul(argv[4], 0, 0) : 4;
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }
	void *map = mmap(NULL, 0x30000000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) { perror("mmap"); return 1; }
	for (int i = 0; i < count; i++)
		*(volatile uint32_t *)((char *)map + addr + i * stride) = val;
	return 0;
}
