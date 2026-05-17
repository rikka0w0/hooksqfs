.PHONY: clean install-deps

BITS ?= 32

ifeq ($(BITS),32)
  ARCH_CFLAGS := -m32
else ifeq ($(BITS),64)
  ARCH_CFLAGS := -m64
else
  $(error BITS must be 32 or 64)
endif

CFLAGS ?= -fPIC -shared -O2 -Wall -Wextra
LDFLAGS ?= -ldl

libhooksqfs.so: hooksqfs.c logging.c
	gcc $(ARCH_CFLAGS) $(CFLAGS) -o $@ hooksqfs.c logging.c $(LDFLAGS)

install-deps:
	sudo apt install linux-libc-dev:i386

clean:
	rm -f libhooksqfs.so
