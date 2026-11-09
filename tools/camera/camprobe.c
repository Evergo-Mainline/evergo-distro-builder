#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>

static volatile uint32_t *m;
static unsigned long base;
static inline void W(unsigned long o, uint32_t v){m[(0x1a030000-base+o)/4]=v;}
static inline uint32_t R(unsigned long o){return m[(0x1a030000-base+o)/4];}

int main(int argc,char**argv){
	int fd=open("/dev/mem",O_RDWR|O_SYNC);
	base=0x10000000;
	m=mmap(NULL,0x20000000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,base);
	if(m==MAP_FAILED){perror("mmap");return 1;}
	/* physical address from debugfs */
	unsigned long phys=0;
	FILE *f=fopen("/sys/kernel/debug/camraw/size","r");
	char line[128]; int n=0;
	while(f&&fgets(line,sizeof(line),f)){ if(n==1) sscanf(line,"%lx",&phys); n++; }
	if(f)fclose(f);
	if(!phys){printf("no phys\n");return 1;}
	unsigned long xsize = argc>1? strtoul(argv[1],0,16):0x1fdf;
	unsigned long stride = argc>2? strtoul(argv[2],0,16):0x13f0;
	uint32_t ctl_en = argc>3? strtoul(argv[3],0,16):0x0516ffef;
	printf("phys=%lx xsize=%lx stride=%lx ctl_en=%x\n",phys,xsize,stride,ctl_en);
	/* reset */
	W(0x007c,0x4); usleep(1000); W(0x007c,0x0);
	/* TG */
	W(0x1f00,0x00016001); W(0x1f08,0x0ff00000); W(0x1f0c,0x0c000000);
	W(0x1f10,0x00008000); W(0x1f14,0x00001430); W(0x1f18,0x0ff00000);
	W(0x1f24,0x00000010); W(0x1f28,0x0fffffff); W(0x1f2c,0x00000020);
	W(0x1f30,0x00bf3ffc); W(0x1f38,0x0ff00c01); W(0x1f64,0x00000019);
	W(0x1f78,0x00564828); W(0x1f7c,0x0ff0056a); W(0x1f80,0x00000001);
	W(0x0008,0x3a3ffd88); W(0x000c,0x01ebb01e); W(0x0018,0x00000308);
	/* CTL sel/fmt */
	W(0x0040,0x06072d90); W(0x0044,0x00000003); W(0x0048,0x01081091);
	W(0x004c,0xfc000928); W(0x0054,0x00360075); W(0x0060,0x000b6005);
	W(0x0200,0x0); /* CQ disable */
	/* IMGO */
	W(0x4820,(uint32_t)phys); W(0x4828,0x84000400);
	W(0x482c,(uint32_t)xsize); W(0x4830,0x00000bff);
	W(0x4834,(uint32_t)stride); W(0x4838,0x80000600);
	W(0x483c,0x03000300); W(0x4840,0x01800180); W(0x4848,0x03000300);
	W(0x4874,0x0c000ff0); W(0x4878,(uint32_t)phys);
	/* enable */
	W(0x0100,(1<<14)|(1<<6)|(1<<0));
	W(0x0014,0x1); /* DMA_EN IMGO only */
	W(0x0004,ctl_en);
	/* start */
	W(0x1f04,0); usleep(1000); W(0x1f04,1);
	for(int i=0;i<40;i++){
		uint32_t st=R(0x0104);
		uint32_t vm=R(0x0000);
		printf("t=%d INT=%08x CTL_STAT=%08x START_ST=%08x 1f3c=%08x\n",i,st,vm,R(0x0078),R(0x1f3c));
		if(st & (1<<14)){ printf("PASS1_DON seen\n"); W(0x0104,st); break; }
		usleep(50000);
	}
	W(0x1f04,0);
	return 0;
}
