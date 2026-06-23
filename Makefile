.PHONY: clean get-deps get-libsquashfs-i386 build-funchook64 build-funchook32

BITS ?= 32

ifeq ($(BITS),32)
  ARCH_CFLAGS := -m32
else ifeq ($(BITS),64)
  ARCH_CFLAGS := -m64
else
  $(error BITS must be 32 or 64)
endif

PKG_CONFIG_CMD := pkg-config
SQFS_CFLAGS := $(shell $(PKG_CONFIG_CMD) --cflags libsquashfs1 2>/dev/null)
SQFS_LIBS := $(shell $(PKG_CONFIG_CMD) --libs libsquashfs1 2>/dev/null)

ifeq ($(BITS),32)
ifneq ($(wildcard $(CURDIR)/libsquashfs-root/usr/include/sqfs/super.h),)
  SQFS_CFLAGS := -I$(CURDIR)/libsquashfs-root/usr/include
  SQFS_LIBS := -L$(CURDIR)/libsquashfs-root/usr/lib/i386-linux-gnu -lsquashfs -lpthread
endif
endif

LDLIBS = $(SQFS_LIBS) \
			-pthread \
			-Lfunchook/build-x86_$(BITS) -lfunchook

INCLUDES := -I$/uthash/include \
			-I$/funchook/include

CFLAGS ?= -fPIC -O2 -Wall -Wextra
CFLAGS_LIB = $(CFLAGS) -pthread $(SQFS_CFLAGS) -shared
LDFLAGS ?= -Wl,--no-as-needed
LDFLAGS_LIB = $(LDFLAGS) -ldl -Wl,-rpath,'$$ORIGIN/funchook/build-x86_$(BITS)'
ifeq ($(BITS),32)
  LDFLAGS_LIB += -Wl,-rpath,'$$ORIGIN/libsquashfs-root/usr/lib/i386-linux-gnu'
endif
SOURCES := hooksqfs.c logging.c utils.c sqfs_mgr.c

libhooksqfs.so: $(SOURCES) funchook/build-x86_$(BITS)/libfunchook.so
	gcc $(ARCH_CFLAGS) $(CFLAGS_LIB) -o $@ $(SOURCES) $(INCLUDES) $(LDFLAGS_LIB) $(LDLIBS)

funchook/build-x86_64/libfunchook.so:
	$(MAKE) build-funchook64

funchook/build-x86_32/libfunchook.so:
	$(MAKE) build-funchook32

build-funchook64:
	rm -rf funchook/build-x86_64/ && mkdir -p funchook/build-x86_64/
	cmake -S funchook -B funchook/build-x86_64 \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DFUNCHOOK_INSTALL=OFF \
		-DFUNCHOOK_CPU=x86 \
		-DFUNCHOOK_BUILD_TESTS=OFF
	cmake --build funchook/build-x86_64 -j"$(nproc)"

build-funchook32:
	rm -rf funchook/build-x86_32/ && mkdir -p funchook/build-x86_32/
	cmake -S funchook -B funchook/build-x86_32 \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DFUNCHOOK_INSTALL=OFF \
		-DFUNCHOOK_CPU=x86 \
		-DFUNCHOOK_BUILD_TESTS=OFF \
		-DCMAKE_C_FLAGS="-m32" \
		-DCMAKE_CXX_FLAGS="-m32" \
		-DCMAKE_ASM_FLAGS="-m32" \
		-DCMAKE_EXE_LINKER_FLAGS="-m32" \
		-DCMAKE_SHARED_LINKER_FLAGS="-m32"
	cmake --build funchook/build-x86_32 -j"$$(nproc)"

# Ubuntu x86_64 does not have 32-bit libsquashfs-dev, so we need to install them manually
get-libsquashfs-i386:
	rm -r ./libsquashfs-root || true
	mkdir -p ./libsquashfs-root
	cd ./libsquashfs-root && \
		wget http://ftp.debian.org/debian/pool/main/s/squashfs-tools-ng/libsquashfs1_1.2.0-1_i386.deb && \
		wget http://ftp.debian.org/debian/pool/main/s/squashfs-tools-ng/libsquashfs-dev_1.2.0-1_i386.deb && \
		for f in *.deb; do dpkg-deb -x "$$f" .; done

get-deps:
	sudo apt install -y build-essential cmake pkg-config gcc-multilib libsquashfs-dev
	sudo apt install -y linux-libc-dev:i386 zlib1g-dev:i386 liblzma-dev:i386 liblz4-dev:i386 libzstd-dev:i386
	sudo apt install -y libsquashfs-dev:i386 || \
		echo "You may need to run 'make get-libsquashfs-i386' to get the 32-bit libsquashfs-dev package"

clean:
	rm -f libhooksqfs.so
