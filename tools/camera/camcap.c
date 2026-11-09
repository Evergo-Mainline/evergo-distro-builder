// camcap - minimal V4L2 MMAP capture: grab one frame from /dev/videoN
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>
#include <stdint.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/video0";
	setvbuf(stdout, NULL, _IONBF, 0);
	fprintf(stderr, "opening %s\n", dev);
	int fd = open(dev, O_RDWR | O_NONBLOCK);
	if (fd < 0) { perror(dev); return 1; }

	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	fprintf(stderr, "QUERYCAP...\n");
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
		printf("driver=%s card=%s caps=%08x\n", cap.driver, cap.card, cap.capabilities);

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fprintf(stderr, "G_FMT...\n");
	if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0)
		printf("G_FMT %ux%u fourcc=%08x planes=%u\n",
		       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		       fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.num_planes);

	/* force the full-resolution sensor format */
	fmt.fmt.pix_mp.width = 4080;
	fmt.fmt.pix_mp.height = 3072;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_SGRBG10;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	fprintf(stderr, "S_FMT 4080x3072 SGRBG10...\n");
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT"); return 1; }
	if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0)
		printf("after S_FMT %ux%u fourcc=%08x planes=%u\n",
		       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		       fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.num_planes);

	struct v4l2_requestbuffers rb;
	memset(&rb, 0, sizeof(rb));
	rb.count = 4; rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	rb.memory = V4L2_MEMORY_MMAP;
	fprintf(stderr, "REQBUFS...\n");
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) { perror("REQBUFS"); return 1; }
	printf("buffers=%u\n", rb.count);

	struct { void *p; size_t len; } bufs[4];
	struct v4l2_plane planes[4];
	for (unsigned i = 0; i < rb.count; i++) {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b)); memset(planes, 0, sizeof(planes));
		b.type = rb.type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
		b.length = 1; b.m.planes = planes;
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); return 1; }
		bufs[i].len = planes[0].length;
		bufs[i].p = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
				 MAP_SHARED, fd, planes[0].m.mem_offset);
		if (bufs[i].p == MAP_FAILED) { perror("mmap"); return 1; }
		printf("buf%u len=%zu\n", i, bufs[i].len);
		memset(&b, 0, sizeof(b)); memset(planes, 0, sizeof(planes));
		b.type = rb.type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
		b.length = 1; b.m.planes = planes;
		if (ioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 1; }
	}

	enum v4l2_buf_type type = rb.type;
	fprintf(stderr, "STREAMON...\n");
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); return 1; }
	printf("streaming...\n");

	for (int t = 0; t < 100; t++) {
		fd_set fds; struct timeval tv;
		FD_ZERO(&fds); FD_SET(fd, &fds);
		tv.tv_sec = 1; tv.tv_usec = 0;
		int r = select(fd + 1, &fds, NULL, NULL, &tv);
		if (r < 0) { perror("select"); break; }
		if (r == 0) { printf("no frame yet (%d s)\n", t + 1); continue; }
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b)); memset(planes, 0, sizeof(planes));
		b.type = type; b.memory = V4L2_MEMORY_MMAP;
		b.length = 1; b.m.planes = planes;
		if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) { perror("DQBUF"); break; }
		printf("GOT FRAME index=%u seq=%u bytesused=%u\n",
		       b.index, b.sequence, planes[0].bytesused);
		if (b.index < 4 && bufs[b.index].p != MAP_FAILED) {
			FILE *f = fopen("/root/frame0.bin", "wb");
			if (f) {
				fwrite(bufs[b.index].p, 1,
				       planes[0].bytesused ? planes[0].bytesused : bufs[b.index].len, f);
				fclose(f);
				printf("saved /root/frame0.bin (%u bytes)\n",
				       planes[0].bytesused ?: (unsigned)bufs[b.index].len);
			}
		}
		ioctl(fd, VIDIOC_QBUF, &b);
		ioctl(fd, VIDIOC_STREAMOFF, &type);
		close(fd);
		return 0;
	}
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	close(fd);
	printf("NO FRAME\n");
	return 2;
}
