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

# Example usage
```bash
mkdir -p /tmp/sqfs-root
cp "left4dead2/addons/miku hatsune replace witch.vpk" /tmp/sqfs-root/
mksquashfs /tmp/sqfs-root output.sqfs -noappend

LD_PRELOAD=/tmp/hook/libhooksqfs.so \
LD_LIBRARY_PATH=/tmp/hook/libsquashfs-root/usr/lib/i386-linux-gnu \
HOOKSQFS_FILE=output.sqfs \
HOOKSQFS_LOG_EXCLUDE=access,readdir,readdir64,closedir \
./srcds_run
```