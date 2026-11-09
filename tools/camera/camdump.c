/* camdump: dump a physical register range from /dev/mem (mmap window 0..0x30000000) */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s addr count\n", argv[0]); return 1; }
	unsigned long addr = strtoul(argv[1], 0, 0);
	unsigned long count = strtoul(argv[2], 0, 0);
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }
	void *map = mmap(NULL, 0x30000000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) { perror("mmap"); return 1; }
	for (unsigned long i = 0; i < count; i++) {
		uint32_t v = *(volatile uint32_t *)((char *)map + addr + i * 4);
		fwrite(&v, 4, 1, stdout);
	}
	return 0;
}
