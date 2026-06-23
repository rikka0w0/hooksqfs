# hooksqfs

hooksqfs implements a pure userspace overlay-like filesystem for selected 32-bit
Linux programs by combining `LD_PRELOAD` with inline hooks. A squashfs image is
used as the read-only lower layer, and the real filesystem remains the writable
upper layer.

The motivation is to mount squashfs-packed L4D2 server maps in restricted
container environments where root mount permissions and FUSE are unavailable,
while saving disk space. This project is not intended to be a general-purpose
userspace replacement for squashfs + overlayfs. It only implements the basic
hooks needed for L4D2 server map loading. The author is not responsible for any
losses caused by using this project.

# Build
```bash
# 32-bit on Ubuntu 24.04
make get-deps
make get-libsquashfs-i386
make BITS=32

# 64-bit on Ubuntu 24.04
make get-deps
make BITS=64
```

# Environment variables

`HOOKSQFS_FILE` sets the squashfs image path. Use an absolute path.

`HOOKSQFS_PREFIX` sets the real filesystem directory that should be overlaid by
the squashfs image. Paths outside this prefix are passed through to the real
filesystem. Paths inside this prefix use the real filesystem first, then fall
back to the squashfs image when the real path does not exist.

`HOOKSQFS_LOG_FILE` sets the log output path. If this variable is unset or empty,
logs are written to stdout. If it is set, the file is opened with truncate
semantics when the first log message is written.

`HOOKSQFS_LOG_INCLUDE` enables per-hook debug logs. Hook logs are disabled by
default. Set it to a comma-separated list such as `open,readdir,close`, or use
`ALL` to enable every hook log. Both full hook names like `sqfs_readdir` and
short names like `readdir` are accepted.

The concurrent test script also accepts these optional variables:

`HOOKSQFS_TEST_FILE` sets the squashfs image used by the test.

`HOOKSQFS_TEST_PREFIX` sets the overlay prefix used by the test.

`HOOKSQFS_TEST_PATH` sets the file path to read concurrently.

`HOOKSQFS_TEST_THREADS` sets the number of reader threads.

`HOOKSQFS_TEST_ITERS` sets the number of read iterations per thread.

# Example usage
```bash
mkdir -p /tmp/sqfs-root
cp "left4dead2/addons/miku hatsune replace witch.vpk" /tmp/sqfs-root/
mksquashfs /tmp/sqfs-root output.sqfs -noappend

LD_PRELOAD=/tmp/hook/libhooksqfs.so \
LD_LIBRARY_PATH=/tmp/hook/libsquashfs-root/usr/lib/i386-linux-gnu \
HOOKSQFS_FILE=output.sqfs \
HOOKSQFS_PREFIX=/path/to/left4dead2/addons \
HOOKSQFS_LOG_FILE=/tmp/hooksqfs.log \
HOOKSQFS_LOG_INCLUDE=ALL \
./srcds_run
```
