// camring.c - replay a 439-entry CQ ring (with cmd bits) on mainline CAM_A
// usage: camring [ringfile] [--zero-cmd] [--single]
// reads IOVA from /sys/kernel/debug/camraw/size (2nd line), writes ring to
// /sys/kernel/debug/camraw/frame, programs CAM_A CQ registers and triggers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>
#define CAM_BASE 0x1a030000UL
#define CQ_DESC_SIZE 0xDB8
static volatile uint32_t *mmio;
static inline void W(unsigned long o, uint32_t v){ mmio[(CAM_BASE-0x10000000+o)/4]=v; }
static inline uint32_t R(unsigned long o){ return mmio[(CAM_BASE-0x10000000+o)/4]; }
static void cam_reset(void){
	W(0x7c,0); usleep(800); W(0x7c,1);
	for(int i=0;i<50;i++){ if(R(0x7c)&2) break; usleep(200); }
	W(0x7c,4); usleep(800); W(0x7c,0); usleep(800);
}
int main(int argc,char**argv){
	const char *ring="/root/cqring.bin"; int zero_cmd=0,single=0;
	for(int i=1;i<argc;i++){
		if(!strcmp(argv[i],"--zero-cmd")) zero_cmd=1;
		else if(!strcmp(argv[i],"--single")) single=1;
		else ring=argv[i];
	}
	unsigned char *b=malloc(CQ_DESC_SIZE);
	FILE *rf=fopen(ring,"rb");
	if(!rf){ perror(ring); return 1; }
	if(fread(b,1,CQ_DESC_SIZE,rf)!=(size_t)CQ_DESC_SIZE&&!single){ printf("ring too small\n"); return 1; }
	fclose(rf);
	if(zero_cmd){ for(int i=0;i<CQ_DESC_SIZE/8;i++){ uint32_t *w=(uint32_t*)(b+i*8); *w &= 0xF800FFFFu; } }
	if(single){ /* keep first descriptor, END at slot 438, dummies between */
		for(int i=1;i<438;i++){ uint32_t *w=(uint32_t*)(b+i*8); w[0]=(((0x1a24a000u+((uint32_t)i<<28))&0xF8000000u)|0x4494u); w[1]=0; }
		uint32_t *e=(uint32_t*)(b+438*8); e[0]=0x06000000; e[1]=0x06000000;
	}
	/* find IOVA */
	unsigned long iova=0; char line[256]; int n=0;
	FILE *sf=fopen("/sys/kernel/debug/camraw/size","r");
	while(sf && fgets(line,sizeof(line),sf)){ n++; if(n==2){ sscanf(line,"%lx",&iova); } }
	if(sf) fclose(sf);
	if(!iova){ printf("no iova from camraw/size\n"); return 1; }
	printf("iova=%lx desc_size=%x\n",iova,CQ_DESC_SIZE);
	int ff=open("/sys/kernel/debug/camraw/frame",O_RDWR);
	if(ff<0){ perror("frame"); return 1; }
	if(write(ff,b,CQ_DESC_SIZE)!=(ssize_t)CQ_DESC_SIZE){ perror("write ring"); return 1; }
	close(ff);
	int fd=open("/dev/mem",O_RDWR|O_SYNC);
	if(fd<0){ perror("/dev/mem"); return 1; }
	mmio=mmap(NULL,0x20000000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x10000000);
	if(mmio==MAP_FAILED){ perror("mmap"); return 1; }
	cam_reset();
	W(0x200,0x112); W(0x204,0xffffffffu);
	W(0x210,0x21); W(0x214,(uint32_t)iova); W(0x218,CQ_DESC_SIZE);
	W(0x74,0); usleep(800); W(0x74,1); usleep(200000);
	printf("after: INT6=%08x INT7=%08x ST=%08x CQ_TRIG=%08x\n",
		R(0x154), R(0x158), R(0x78), R(0x74));
	printf("INT6_EN=%08x INT7_EN=%08x CQ_EN=%08x THR0=%08x SIZE=%08x\n",
		R(0x150), R(0x15c), R(0x200), R(0x210), R(0x218));
	int nz=0; for(int i=0;i<CQ_DESC_SIZE/8 && i<32;i++){ uint32_t *w=(uint32_t*)(b+i*8); if(w[0]||w[1]) nz++; }
	printf("ring[0..%d] nonzero=%d ring0=%08x/%08x ring1=%08x/%08x\n",32,nz,
		((uint32_t*)b)[0],((uint32_t*)b)[1],((uint32_t*)b)[2],((uint32_t*)b)[3]);
	return 0;
}
