.PHONY: clean get-deps get-libsquashfs-i386

BITS ?= 32

ifeq ($(BITS),32)
  ARCH_CFLAGS := -m32
else ifeq ($(BITS),64)
  ARCH_CFLAGS := -m64
else
  $(error BITS must be 32 or 64)
endif

PKG_CONFIG_CMD := pkg-config
ifeq ($(BITS),32)
ifneq ($(wildcard $(CURDIR)/libsquashfs-root),)
  PKG_CONFIG_CMD := PKG_CONFIG_LIBDIR=$(CURDIR)/libsquashfs-root/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig \
    pkg-config --define-variable=prefix=$(CURDIR)/libsquashfs-root/usr
endif
endif

LDLIBS = $(shell $(PKG_CONFIG_CMD) --cflags --libs libsquashfs1)

INCLUDES := -I$(CURDIR)/uthash/include

CFLAGS ?= -fPIC -O2 -Wall -Wextra
CFLAGS_LIB = $(CFLAGS) -shared
LDFLAGS ?= -Wl,--no-as-needed
LDFLAGS_LIB = $(LDFLAGS) -ldl

SOURCES := hooksqfs.c logging.c hashmap.c sqfs_file.c

libhooksqfs.so: $(SOURCES)
	gcc $(ARCH_CFLAGS) $(CFLAGS_LIB) -o $@ $(SOURCES) $(INCLUDES) $(LDFLAGS_LIB) $(LDLIBS)

test: $(SOURCES)
	gcc $(ARCH_CFLAGS) $(CFLAGS) -o $@ $(SOURCES) $(INCLUDES) $(LDLIBS)

# Ubuntu x86_64 does not have 32-bit libsquashfs-dev, so we need to install them manually
get-libsquashfs-i386:
	rm -r ./libsquashfs-root || true
	mkdir -p ./libsquashfs-root
	cd ./libsquashfs-root && \
		wget http://ftp.debian.org/debian/pool/main/s/squashfs-tools-ng/libsquashfs1_1.2.0-1_i386.deb && \
		wget http://ftp.debian.org/debian/pool/main/s/squashfs-tools-ng/libsquashfs-dev_1.2.0-1_i386.deb && \
		for f in *.deb; do dpkg-deb -x "$$f" .; done

get-deps:
	sudo apt install -y pkg-config libsquashfs-dev
	sudo apt install -y linux-libc-dev:i386 zlib1g-dev:i386 liblzma-dev:i386 liblz4-dev:i386 libzstd-dev:i386
	sudo apt install -y libsquashfs-dev:i386 || \
		echo "You may need to run 'make get-libsquashfs-i386' to get the 32-bit libsquashfs-dev package"

clean:
	rm -f libhooksqfs.so
	rm -f test
