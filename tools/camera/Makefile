# Cross-compile camera debug tools for aarch64 (static, run on device)
CC ?= aarch64-linux-gnu-gcc
CFLAGS ?= -O2 -static -Wall

TOOLS := camcap campeek campoke camdump camgraph caminfo camprobe

all: $(TOOLS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TOOLS)

.PHONY: all clean
