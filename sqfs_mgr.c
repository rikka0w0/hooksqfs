#define _GNU_SOURCE

#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <execinfo.h>

#include <sqfs/compressor.h>
#include <sqfs/data_reader.h>
#include <sqfs/dir.h>
#include <sqfs/dir_reader.h>
#include <sqfs/inode.h>
#include <sqfs/super.h>
#include <sqfs/io.h>
#include <sqfs/error.h>

#include "utils.h"
#include "logging.h"
#include "real.h"

#include "uthash.h"
#include "utlist.h"

struct SqfsDirEntryNode {
	sqfs_inode_generic_t *inode;
	struct dirent dirent;
	struct SqfsDirEntryNode *next;
};

struct DirMapEntry {
	DIR *key; // The underlying DIR* pointer.
	sqfs_dir_reader_t *dir_reader;
	bool is_real_dir;
	struct SqfsDirEntryNode* dir_entry_list;

	// For readdir
	struct SqfsDirEntryNode *dir_entry_current;
	bool dir_entry_list_reached_end;

	UT_hash_handle hh;
};

struct FDMapEntry {
	int key; // The underlying file descriptor.
	UT_hash_handle hh;

	// The corresponding squashfs inode
	sqfs_inode_generic_t *inode;

	uint64_t offset;
};

static struct sqfs_mgr {
	pthread_mutex_t mutex;

	// Do not inteprete the other fields if field file is NULL.
	sqfs_file_t *file;
	sqfs_compressor_t *compressor;
	sqfs_data_reader_t *data_reader;

	sqfs_super_t super;
	sqfs_compressor_config_t cfg;

	struct DirMapEntry *dir_map;
	struct FDMapEntry *fd_map;
} g_xSqfsMgr = {0};

void sqfs_mutex_init_once(void)
{
	pthread_mutexattr_t attr;

	/*
	 * libsquashfs can call libc read/pread/lseek while we already hold this
	 * lock. Those libc entries are also hooked, so same-thread reentry must
	 * pass through and fall back to the real libc function.
	 */
	if (pthread_mutexattr_init(&attr) != 0) {
		pthread_mutex_init(&g_xSqfsMgr.mutex, NULL);
		return;
	}

	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0) {
		pthread_mutex_init(&g_xSqfsMgr.mutex, &attr);
	} else {
		pthread_mutex_init(&g_xSqfsMgr.mutex, NULL);
	}

	pthread_mutexattr_destroy(&attr);
}

static void sqfs_lock(void)
{
	pthread_mutex_lock(&g_xSqfsMgr.mutex);
}

static void sqfs_unlock(void)
{
	pthread_mutex_unlock(&g_xSqfsMgr.mutex);
}

static bool sqfs_mgr_loaded_locked(void)
{
	return g_xSqfsMgr.file != NULL &&
	       g_xSqfsMgr.compressor != NULL &&
	       g_xSqfsMgr.data_reader != NULL;
}

static const char *sqfs_mgr_get_prefix(void)
{
	const char *prefix = getenv("HOOKSQFS_PREFIX");

	return prefix != NULL ? prefix : "/hook";
}

static bool open_flags_need_mode(int flags)
{
#ifdef O_TMPFILE
	if ((flags & O_TMPFILE) == O_TMPFILE) {
		return true;
	}
#endif

	return (flags & O_CREAT) != 0;
}

static int errno_from_sqfs(int ret)
{
	switch (ret) {
	case SQFS_ERROR_ALLOC:
		return ENOMEM;
	case SQFS_ERROR_NO_ENTRY:
		return ENOENT;
	case SQFS_ERROR_NOT_DIR:
		return ENOTDIR;
	case SQFS_ERROR_NOT_FILE:
		return EISDIR;
	case SQFS_ERROR_UNSUPPORTED:
		return ENOTSUP;
	case SQFS_ERROR_ARG_INVALID:
		return EINVAL;
	default:
		return EIO;
	}
}

static const char *sqfs_compressor_name_readable(SQFS_COMPRESSOR id)
{
	const char *name = sqfs_compressor_name_from_id(id);
	return name != NULL ? name : "unknown";
}

const char *sqfs_error_string(int err)
{
	switch (err) {
	case 0: return "success";
	case SQFS_ERROR_ALLOC: return "allocation failed";
	case SQFS_ERROR_IO: return "I/O error";
	case SQFS_ERROR_COMPRESSOR: return "compressor error";
	case SQFS_ERROR_INTERNAL: return "internal error";
	case SQFS_ERROR_CORRUPTED: return "corrupted filesystem";
	case SQFS_ERROR_UNSUPPORTED: return "unsupported feature";
	case SQFS_ERROR_OVERFLOW: return "numeric overflow";
	case SQFS_ERROR_OUT_OF_BOUNDS: return "out-of-bounds read";
	case SFQS_ERROR_SUPER_MAGIC: return "bad superblock magic";
	case SFQS_ERROR_SUPER_VERSION: return "unsupported superblock version";
	case SQFS_ERROR_SUPER_BLOCK_SIZE: return "invalid superblock block size";
	case SQFS_ERROR_NOT_DIR: return "not a directory";
	case SQFS_ERROR_NO_ENTRY: return "no such entry";
	case SQFS_ERROR_LINK_LOOP: return "hard link loop";
	case SQFS_ERROR_NOT_FILE: return "not a regular file";
	case SQFS_ERROR_ARG_INVALID: return "invalid argument";
	case SQFS_ERROR_SEQUENCE: return "invalid operation sequence";
	default: return "unknown libsquashfs error";
	}
}

static void sqfs_util_log_failure(const char* func, int err)
{
	log_msg("libsquashfs %s returned %d (%s)\n", func, err, sqfs_error_string(err));
}

void sqfs_util_super_info(const sqfs_super_t *super)
{
	log_msg("libsquashfs info:\n");
	log_msg("inodes: %u\n", super->inode_count);
	log_msg("block size: %u\n", super->block_size);
	log_msg("bytes used: %llu\n", (unsigned long long)super->bytes_used);
	log_msg("compression: %s\n", sqfs_compressor_name_readable(super->compression_id));
}

/*
 * Caller must hold g_xSqfsMgr.mutex before calling this function.
 */
static void sqfs_mgr_unload_image(void)
{
	if (g_xSqfsMgr.data_reader) {
		sqfs_destroy(g_xSqfsMgr.data_reader);
		g_xSqfsMgr.data_reader = NULL;
	}

	if (g_xSqfsMgr.compressor) {
		sqfs_destroy(g_xSqfsMgr.compressor);
		g_xSqfsMgr.compressor = NULL;
	}

	sqfs_free(g_xSqfsMgr.file);
	g_xSqfsMgr.file = NULL;
}

bool sqfs_mgr_load_image(void)
{
	bool ok = false;

	sqfs_lock();

	if (sqfs_mgr_loaded_locked()) {
		ok = true;
		goto out;
	}

	const char *sqfs_path = getenv("HOOKSQFS_FILE");
	if (!sqfs_path) {
		log_msg("HOOKSQFS_FILE not set\n");
		goto out;
	}

	g_xSqfsMgr.file = sqfs_open_file(sqfs_path, SQFS_FILE_OPEN_READ_ONLY);
	if (g_xSqfsMgr.file == NULL) {
		log_msg("sqfs_open_file failed\n");
		goto out;
	}

	int ret = sqfs_super_read(&g_xSqfsMgr.super, g_xSqfsMgr.file);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_super_read", ret);
		sqfs_mgr_unload_image();
		goto out;
	}

	log_msg("libsquashfs read OK\n");
	sqfs_util_super_info(&g_xSqfsMgr.super);

	ret = sqfs_compressor_config_init(&g_xSqfsMgr.cfg, 
		g_xSqfsMgr.super.compression_id,
		g_xSqfsMgr.super.block_size,
		SQFS_COMP_FLAG_UNCOMPRESS);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_compressor_config_init", ret);
		sqfs_mgr_unload_image();
		goto out;
	}

	ret = sqfs_compressor_create(&g_xSqfsMgr.cfg, &g_xSqfsMgr.compressor);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_compressor_create", ret);
		sqfs_mgr_unload_image();
		goto out;
	}

	if (g_xSqfsMgr.super.flags & SQFS_FLAG_COMPRESSOR_OPTIONS) {
		ret = g_xSqfsMgr.compressor->read_options(g_xSqfsMgr.compressor, g_xSqfsMgr.file);
		if (ret != 0) {
			sqfs_util_log_failure("sqfs_compressor_read_options", ret);
			sqfs_mgr_unload_image();
			goto out;
		}
	}

	g_xSqfsMgr.data_reader = sqfs_data_reader_create(
		g_xSqfsMgr.file, g_xSqfsMgr.super.block_size, g_xSqfsMgr.compressor, 0);
	if (g_xSqfsMgr.data_reader == NULL) {
		log_msg("sqfs_data_reader_create failed\n");
		sqfs_mgr_unload_image();
		goto out;
	}

	ret = sqfs_data_reader_load_fragment_table(
		g_xSqfsMgr.data_reader, &g_xSqfsMgr.super);
	if (ret != 0) {
		log_msg("sqfs_data_reader_load_fragment_table failed: %d\n", ret);
		sqfs_mgr_unload_image();
		goto out;
	}

	ok = true;

out:
	sqfs_unlock();
	return ok;
}

static int sqfs_check_path_then_convert(const char *pathname, char* relative_out, size_t len)
{
	const char *prefix = sqfs_mgr_get_prefix();
	const char *sqfs_path = getenv("HOOKSQFS_FILE");

	if (pathname == NULL) {
		return EFAULT;
	}

	if (path_equals_normalized(pathname, sqfs_path)) {
		return ENOENT;
	}

	if (!path_relative_to_root(prefix, pathname, relative_out, len)) {
		return ENOENT;
	}

	return 0;
}

static sqfs_inode_generic_t *sqfs_find_inode(const char *relative)
{
	if (relative == NULL) {
		errno = EFAULT;
		return NULL;
	}

	if (!sqfs_mgr_loaded_locked()) {
		errno = ENOENT;
		return NULL;
	}

	sqfs_dir_reader_t *dir_reader = sqfs_dir_reader_create(
		&g_xSqfsMgr.super, g_xSqfsMgr.compressor, g_xSqfsMgr.file, 0);
	if (dir_reader == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	sqfs_inode_generic_t *inode = NULL;
	int ret;
	if (relative[0] == '\0') {
		ret = sqfs_dir_reader_get_root_inode(dir_reader, &inode);
	} else {
		ret = sqfs_dir_reader_find_by_path(dir_reader, NULL, relative, &inode);
	}

	sqfs_destroy(dir_reader);
	if (ret != 0) {
		errno = errno_from_sqfs(ret);
		return NULL;
	}

	return inode;
}

static mode_t sqfs_inode_mode(const sqfs_inode_generic_t *inode)
{
	mode_t mode = inode->base.mode & 07777;

	switch (inode->base.type) {
	case SQFS_INODE_DIR:
	case SQFS_INODE_EXT_DIR:
		return mode | S_IFDIR;
	case SQFS_INODE_FILE:
	case SQFS_INODE_EXT_FILE:
		return mode | S_IFREG;
	case SQFS_INODE_SLINK:
	case SQFS_INODE_EXT_SLINK:
		return mode | S_IFLNK;
	case SQFS_INODE_BDEV:
	case SQFS_INODE_EXT_BDEV:
		return mode | S_IFBLK;
	case SQFS_INODE_CDEV:
	case SQFS_INODE_EXT_CDEV:
		return mode | S_IFCHR;
	case SQFS_INODE_FIFO:
	case SQFS_INODE_EXT_FIFO:
		return mode | S_IFIFO;
	case SQFS_INODE_SOCKET:
	case SQFS_INODE_EXT_SOCKET:
		return mode | S_IFSOCK;
	default:
		return mode;
	}
}

static nlink_t sqfs_inode_nlink(const sqfs_inode_generic_t *inode)
{
	switch (inode->base.type) {
	case SQFS_INODE_DIR:
		return inode->data.dir.nlink;
	case SQFS_INODE_EXT_DIR:
		return inode->data.dir_ext.nlink;
	case SQFS_INODE_EXT_FILE:
		return inode->data.file_ext.nlink;
	case SQFS_INODE_SLINK:
		return inode->data.slink.nlink;
	case SQFS_INODE_EXT_SLINK:
		return inode->data.slink_ext.nlink;
	case SQFS_INODE_BDEV:
	case SQFS_INODE_CDEV:
		return inode->data.dev.nlink;
	case SQFS_INODE_EXT_BDEV:
	case SQFS_INODE_EXT_CDEV:
		return inode->data.dev_ext.nlink;
	case SQFS_INODE_FIFO:
	case SQFS_INODE_SOCKET:
		return inode->data.ipc.nlink;
	case SQFS_INODE_EXT_FIFO:
	case SQFS_INODE_EXT_SOCKET:
		return inode->data.ipc_ext.nlink;
	default:
		return 1;
	}
}

static sqfs_u64 sqfs_inode_size(const sqfs_inode_generic_t *inode)
{
	sqfs_u64 size = 0;

	switch (inode->base.type) {
	case SQFS_INODE_FILE:
	case SQFS_INODE_EXT_FILE:
		if (sqfs_inode_get_file_size(inode, &size) != 0)
			return 0;
		return size;
	case SQFS_INODE_DIR:
		return inode->data.dir.size;
	case SQFS_INODE_EXT_DIR:
		return inode->data.dir_ext.size;
	case SQFS_INODE_SLINK:
		return inode->data.slink.target_size;
	case SQFS_INODE_EXT_SLINK:
		return inode->data.slink_ext.target_size;
	default:
		return 0;
	}
}

static void sqfs_fill_stat(const sqfs_inode_generic_t *inode, struct stat *buf)
{
	sqfs_u64 size = sqfs_inode_size(inode);
	time_t mtime = (time_t)inode->base.mod_time;

	memset(buf, 0, sizeof(*buf));
	buf->st_ino = inode->base.inode_number;
	buf->st_mode = sqfs_inode_mode(inode);
	buf->st_nlink = sqfs_inode_nlink(inode);
	buf->st_uid = 0;
	buf->st_gid = 0;
	buf->st_size = (off_t)size;
	buf->st_blksize = g_xSqfsMgr.super.block_size;
	buf->st_blocks = (blkcnt_t)((size + 511) / 512);
	buf->st_atim.tv_sec = mtime;
	buf->st_mtim.tv_sec = mtime;
	buf->st_ctim.tv_sec = mtime;
}

static bool sqfs_stat_impl(const char *relative, struct stat *buf)
{
	sqfs_inode_generic_t *inode = sqfs_find_inode(relative);
	if (inode == NULL)
		return false;
	// log_hook(__func__, "sqfs_stat found inode for path \"%s\"\n", relative);

	sqfs_fill_stat(inode, buf);
	sqfs_free(inode);
	return true;
}

int sqfs_xstat(int ver, const char *pathname, struct stat *buf) {
	bool ok;
	int saved_errno;

	// Try the real path first
	if (g_xLibcFuncs.__xstat(ver, pathname, buf) == 0) {
		return 0;
	}

	// If the real path does not exist, try sqfs
	char relative[PATH_MAX];
	int ret = sqfs_check_path_then_convert(pathname, relative, sizeof(relative));
	if (ret != 0) {
		errno = ret;
		return -1;
	}

	if (buf == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (ver != 3) {
		log_hook(__func__, "Unsupported ver %d\n", ver);
		errno = EFAULT;
		return -1;
	}

	sqfs_lock();
	ok = sqfs_stat_impl(relative, buf);
	saved_errno = errno;
	sqfs_unlock();

	errno = saved_errno;
	return ok ? 0 : -1;
}

int sqfs_open(const char *pathname, int flags, ...) {
	char relative[PATH_MAX];
	mode_t mode = 0;
	bool has_mode = open_flags_need_mode(flags);
	int fd;
	int ret;

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

		fd = g_xLibcFuncs.open(pathname, flags, mode);
	} else {
		fd = g_xLibcFuncs.open(pathname, flags);
	}

	if (fd >= 0)
		return fd;

	int saved_errno = errno;
	ret = sqfs_check_path_then_convert(pathname, relative, sizeof(relative));
	if (ret != 0) {
		errno = saved_errno;
		return -1;
	}

	sqfs_lock();

	sqfs_inode_generic_t *inode = sqfs_find_inode(relative);
	if (inode == NULL) {
		int find_errno = errno;
		sqfs_unlock();
		log_hook(__func__, "sqfs_find_inode(\"%s\") failed: errno=%d\n",
			relative, find_errno);
		errno = find_errno;
		return -1;
	}

	fd = create_backing_fd(flags);
	if (fd < 0)
		goto error_and_free_inode;

	struct FDMapEntry *fd_entry = malloc(sizeof(struct FDMapEntry));
	if (fd_entry == NULL) {
		errno = ENOMEM;
		goto error_and_free_fd;
	}

	fd_entry->key = fd;
	fd_entry->inode = inode;
	fd_entry->offset = 0;
	HASH_ADD_INT(g_xSqfsMgr.fd_map, key, fd_entry);

	errno = 0;
	sqfs_unlock();

	log_hook(__func__, "ok: relative_path=\"%s\", flags=0x%x, errno=%d => fd=%d\n",
		relative, flags, errno, fd);

	return fd;

error_and_free_fd:
	g_xLibcFuncs.close(fd);

error_and_free_inode:
	sqfs_free(inode);

	sqfs_unlock();

	log_hook(__func__, "failed: relative_path=\"%s\", flags=0x%x, errno=%d\n",
		relative, flags,  errno);

	return -1;
}

ssize_t sqfs_read(int fd, void *buf, size_t count)
{
	struct FDMapEntry *found = NULL;
	ssize_t result;

	sqfs_lock();
	HASH_FIND_INT(g_xSqfsMgr.fd_map, &fd, found);
	if (found == NULL) {
		sqfs_unlock();
		return g_xLibcFuncs.read(fd, buf, count);
	}

	int ret = sqfs_data_reader_read(
		g_xSqfsMgr.data_reader,
		found->inode,
		found->offset,
		buf, count
	);

	if (ret >= 0) {
		// Success or EOF
		log_hook(__func__, "ok: fd=%d, count=%zu@%llu, inode=%u: ret=%d\n",
			fd, count, (unsigned long long)found->offset,
			found->inode->base.inode_number, ret);

		found->offset += (uint64_t)ret;

		errno = 0;
		result = ret;
	} else {
		log_hook(__func__, "failed: fd=%d, count=%zu, inode=%u: %s\n",
			fd, count, found->inode->base.inode_number,
			sqfs_error_string(ret));

		errno = errno_from_sqfs(ret);
		result = -1;
	}

	sqfs_unlock();
	return result;
}

static ssize_t sqfs_pread_mapped(const char *func, struct FDMapEntry *fd_record, void *buf,
				 size_t count, off64_t offset)
{
	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	int ret = sqfs_data_reader_read(
		g_xSqfsMgr.data_reader,
		fd_record->inode,
		(sqfs_u64)offset,
		buf, count
	);

	if (ret >= 0) {
		log_hook(func, "ok: fd=%d, count=%zu@%lld, inode=%u: ret=%d\n",
			fd_record->key, count, (long long)offset,
			fd_record->inode->base.inode_number, ret);

		errno = 0;
		return ret;
	}

	log_hook(func, "failed: fd=%d, count=%zu@%lld, inode=%u: %s\n",
		fd_record->key, count, (long long)offset,
		fd_record->inode->base.inode_number,
		sqfs_error_string(ret));

	errno = errno_from_sqfs(ret);
	return -1;
}

ssize_t sqfs_pread(int fd, void *buf, size_t count, off_t offset)
{
	struct FDMapEntry *found = NULL;
	ssize_t result;

	sqfs_lock();
	HASH_FIND_INT(g_xSqfsMgr.fd_map, &fd, found);
	if (found == NULL) {
		sqfs_unlock();
		return g_xLibcFuncs.pread(fd, buf, count, offset);
	}

	result = sqfs_pread_mapped(__func__, found, buf, count, (off64_t)offset);
	sqfs_unlock();
	return result;
}

ssize_t sqfs_pread64(int fd, void *buf, size_t count, off64_t offset)
{
	struct FDMapEntry *found = NULL;
	ssize_t result;

	sqfs_lock();
	HASH_FIND_INT(g_xSqfsMgr.fd_map, &fd, found);
	if (found == NULL) {
		sqfs_unlock();
		if (g_xLibcFuncs.pread64 != NULL)
			return g_xLibcFuncs.pread64(fd, buf, count, offset);

		errno = ENOSYS;
		return -1;
	}

	result = sqfs_pread_mapped(__func__, found, buf, count, offset);
	sqfs_unlock();
	return result;
}

static uint64_t max_signed_value_for_size(size_t size)
{
	if (size >= sizeof(int64_t))
		return (uint64_t)INT64_MAX;

	return (UINT64_C(1) << (size * CHAR_BIT - 1)) - 1;
}

static int sqfs_seek_add(uint64_t base, off64_t offset, uint64_t *out)
{
	if (offset < 0) {
		uint64_t distance = (uint64_t)(-(offset + 1)) + 1;

		if (distance > base)
			return EINVAL;

		*out = base - distance;
		return 0;
	}

	if (UINT64_MAX - base < (uint64_t)offset)
		return EOVERFLOW;

	*out = base + (uint64_t)offset;
	return 0;
}

static int sqfs_seek_calculate(struct FDMapEntry *fd_record, off64_t offset,
			       int whence, uint64_t *new_offset)
{
	uint64_t size = sqfs_inode_size(fd_record->inode);

	switch (whence) {
	case SEEK_SET:
		return sqfs_seek_add(0, offset, new_offset);
	case SEEK_CUR:
		return sqfs_seek_add(fd_record->offset, offset, new_offset);
	case SEEK_END:
		return sqfs_seek_add(size, offset, new_offset);
#ifdef SEEK_DATA
	case SEEK_DATA:
		if (offset < 0)
			return EINVAL;
		if ((uint64_t)offset >= size)
			return ENXIO;
		*new_offset = (uint64_t)offset;
		return 0;
#endif
#ifdef SEEK_HOLE
	case SEEK_HOLE:
		if (offset < 0)
			return EINVAL;
		if ((uint64_t)offset > size)
			return ENXIO;
		*new_offset = size;
		return 0;
#endif
	default:
		return EINVAL;
	}
}

static int sqfs_lseek_mapped(const char *func, struct FDMapEntry *fd_record,
			     off64_t offset,
			     int whence, uint64_t max_return,
			     uint64_t *new_offset)
{
	uint64_t old_offset = fd_record->offset;
	int err = sqfs_seek_calculate(fd_record, offset, whence, new_offset);

	if (err != 0) {
		errno = err;
		log_hook(func, "failed: fd=%d, offset=%lld, whence=%d, old=%llu, inode=%u, errno=%d\n",
			fd_record->key, (long long)offset, whence,
			(unsigned long long)old_offset,
			fd_record->inode != NULL ? fd_record->inode->base.inode_number : 0,
			err);
		return -1;
	}

	if (*new_offset > max_return) {
		errno = EOVERFLOW;
		log_hook(func, "failed: fd=%d, offset=%lld, whence=%d, old=%llu, new=%llu, inode=%u, errno=%d\n",
			fd_record->key, (long long)offset, whence,
			(unsigned long long)old_offset,
			(unsigned long long)*new_offset,
			fd_record->inode != NULL ? fd_record->inode->base.inode_number : 0,
			EOVERFLOW);
		return -1;
	}

	fd_record->offset = *new_offset;
	errno = 0;
	log_hook(func, "ok: fd=%d, offset=%lld, whence=%d, old=%llu, new=%llu, inode=%u\n",
		fd_record->key, (long long)offset, whence,
		(unsigned long long)old_offset,
		(unsigned long long)*new_offset,
		fd_record->inode != NULL ? fd_record->inode->base.inode_number : 0);
	return 0;
}

off_t sqfs_lseek(int fd, off_t offset, int whence)
{
	struct FDMapEntry *found = NULL;
	uint64_t new_offset = 0;
	off_t result;

	sqfs_lock();
	HASH_FIND_INT(g_xSqfsMgr.fd_map, &fd, found);
	if (found == NULL) {
		sqfs_unlock();
		return g_xLibcFuncs.lseek(fd, offset, whence);
	}

	if (sqfs_lseek_mapped(__func__, found, (off64_t)offset, whence,
			      max_signed_value_for_size(sizeof(off_t)),
			      &new_offset) != 0) {
		sqfs_unlock();
		return (off_t)-1;
	}

	result = (off_t)new_offset;
	sqfs_unlock();
	return result;
}

off64_t sqfs_lseek64(int fd, off64_t offset, int whence)
{
	struct FDMapEntry *found = NULL;
	uint64_t new_offset = 0;
	off64_t result;

	sqfs_lock();
	HASH_FIND_INT(g_xSqfsMgr.fd_map, &fd, found);
	if (found == NULL) {
		sqfs_unlock();
		if (g_xLibcFuncs.lseek64 != NULL)
			return g_xLibcFuncs.lseek64(fd, offset, whence);

		errno = ENOSYS;
		return (off64_t)-1;
	}

	if (sqfs_lseek_mapped(__func__, found, offset, whence,
			      (uint64_t)INT64_MAX, &new_offset) != 0) {
		sqfs_unlock();
		return (off64_t)-1;
	}

	result = (off64_t)new_offset;
	sqfs_unlock();
	return result;
}

static int sqfs_close_impl(const char *func, int fd, int (*real_close)(int))
{
	struct FDMapEntry *found = NULL;
	int ret;
	int saved_errno;
	unsigned int inode_number;

	if (real_close == NULL) {
		errno = ENOSYS;
		return -1;
	}

	sqfs_lock();
	HASH_FIND_INT(g_xSqfsMgr.fd_map, &fd, found);
	if (found == NULL) {
		sqfs_unlock();
		return real_close(fd);
	}

	inode_number =
		found->inode != NULL ? found->inode->base.inode_number : 0;

	HASH_DEL(g_xSqfsMgr.fd_map, found);
	sqfs_free(found->inode);
	free(found);
	sqfs_unlock();

	ret = real_close(fd);
	saved_errno = errno;

	log_hook(func, "fd=%d, inode=%u, ret=%d, errno=%d\n",
		fd, inode_number, ret, ret == 0 ? 0 : saved_errno);

	errno = saved_errno;
	return ret;
}

int sqfs_close(int fd)
{
	return sqfs_close_impl(__func__, fd, g_xLibcFuncs.close);
}

int sqfs_close_nocancel(int fd)
{
	int (*real_close)(int) = g_xLibcFuncs.__close_nocancel != NULL ?
		g_xLibcFuncs.__close_nocancel : g_xLibcFuncs.close;

	return sqfs_close_impl(__func__, fd, real_close);
}

static void free_sqfs_dir_entry_list(struct SqfsDirEntryNode **dir_entry_list) {
	struct SqfsDirEntryNode *current = *dir_entry_list;
	while (current != NULL) {
		struct SqfsDirEntryNode *next = current->next;
		sqfs_free(current->inode);
		free(current);
		current = next;
	}
	*dir_entry_list = NULL;
}

static void enumerate_dir(sqfs_dir_reader_t *dir_reader, struct SqfsDirEntryNode **dir_entry_list) {
	while (true) {
		sqfs_dir_entry_t *dir_entry = NULL;
		int ret = sqfs_dir_reader_read(dir_reader, &dir_entry);
		if (ret > 0)
			return;
		if (ret < 0) {
			sqfs_util_log_failure("sqfs_dir_reader_read", ret);
			break;
		}

		struct SqfsDirEntryNode *node = malloc(sizeof(struct SqfsDirEntryNode));
		if (node == NULL) {
			log_msg("  malloc failed for dir entry\n");
			sqfs_free(dir_entry);
			break;
		}

		node->inode = NULL;
		ret = sqfs_dir_reader_get_inode(dir_reader, &node->inode);
		if (ret != 0) {
			log_msg("  %.*s size=? head16=<inode error %d>\n",
				(int)dir_entry->size + 1, dir_entry->name, ret);
			sqfs_free(dir_entry);
			break;
		}
		
		node->dirent.d_ino = node->inode->base.inode_number;
		node->dirent.d_off = 114514; // Not used
		node->dirent.d_reclen = sizeof(struct dirent);
		node->dirent.d_type = (node->inode->base.type == SQFS_INODE_DIR ||
				       node->inode->base.type == SQFS_INODE_EXT_DIR) ? DT_DIR : DT_REG;
		memcpy(node->dirent.d_name, dir_entry->name, dir_entry->size + 1);
		node->dirent.d_name[dir_entry->size + 1] = '\0';

		LL_APPEND(*dir_entry_list, node);
		// log_msg(" inode %d -> %s\n", node->dirent.d_ino, node->dirent.d_name);
	}

	free_sqfs_dir_entry_list(dir_entry_list);
}

int sqfs_access(const char *pathname, int mode)
{
	bool stat_ok;
	int saved_errno;
	struct stat st = {0};

	if (g_xLibcFuncs.access(pathname, mode) == 0) {
		if (!sqfs_check_path_then_convert(pathname, NULL, 0)) {
			log_hook(__func__, "granted: g_xLibcFuncs.access(path=\"%s\", mode=%d)\n",
				pathname, mode);
		}
		return 0;
	}

	// The real access() returned false, check squashfs
	char relative[PATH_MAX];
	int ret = sqfs_check_path_then_convert(pathname, relative, sizeof(relative));
	if (ret != 0) {
		errno = ret;
		goto return_err;
	}

	sqfs_lock();
	stat_ok = sqfs_stat_impl(relative, &st);
	saved_errno = errno;
	sqfs_unlock();

	if (!stat_ok) {
		errno = saved_errno;
		goto return_err;
	}

	bool can_read = (st.st_mode & 0444) != 0;
	bool can_exec = (st.st_mode & 0111) != 0;

	if ((mode & W_OK) != 0) {
		errno = EROFS;
		goto return_err;
	}
	if ((mode & R_OK) != 0 && !can_read) {
		errno = EACCES;
		goto return_err;
	}
	if ((mode & X_OK) != 0 && !can_exec) {
		errno = EACCES;
		goto return_err;
	}

	log_hook(__func__, "granted: path=\"%s\", mode=%d, st_mode=%o\n",
		pathname, mode, st.st_mode);
	return 0;

return_err:
	if (!sqfs_check_path_then_convert(pathname, NULL, 0)) {
		log_hook(__func__, "denied: path=\"%s\", mode=%d, st_mode=%o, errno=%d\n",
			pathname, mode, st.st_mode, errno);
	}
	return -1;
}

DIR * sqfs_opendir(const char *name) {
	DIR *underlying_dir = g_xLibcFuncs.opendir(name);
	bool is_real_dir = underlying_dir != NULL;
	DIR *result = NULL;

	char relative[PATH_MAX];
	if (sqfs_check_path_then_convert(name, relative, sizeof(relative)) != 0) {
		if (is_real_dir) {
			// The given path exists in the real filesystem, but not part of the squashfs
			errno = 0;
			return underlying_dir;
		} else {
			// The given path really doesn't exist.
			errno = ENOENT;
			return NULL;
		}
	}

	log_hook(__func__, "%s -> %s\n", name, relative);

	sqfs_lock();

	if (!sqfs_mgr_loaded_locked()) {
		if (is_real_dir) {
			errno = 0;
			result = underlying_dir;
		} else {
			errno = ENOENT;
		}
		goto out_unlock_only;
	}

	sqfs_dir_reader_t *dir_reader = sqfs_dir_reader_create(
		&g_xSqfsMgr.super, g_xSqfsMgr.compressor, g_xSqfsMgr.file, 0);
	if (dir_reader == NULL) {
		errno = ENOMEM;
		goto out_unlock_only;
	}

	sqfs_inode_generic_t *inode = sqfs_find_inode(relative);
	if (inode == NULL) {
		int saved_errno = errno;
		if (is_real_dir) {
			// The given path is under prefix, but belong to the real filesystem
			log_hook(__func__, "\"%s\" belong to the upper layer\n", relative);
			sqfs_destroy(dir_reader);
			errno = 0;
			result = underlying_dir;
			goto out_unlock_only;
		}

		log_hook(__func__, "sqfs_find_inode(\"%s\") failed: %d\n", relative, errno);
		errno = saved_errno;
		goto out;
	}

	if (inode->base.type != SQFS_INODE_DIR &&
	       inode->base.type != SQFS_INODE_EXT_DIR) {
		errno = ENOTDIR;
		goto out;
	}

	int ret = sqfs_dir_reader_open_dir(dir_reader, inode, 0);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_dir_reader_open_dir", ret);
		errno = errno_from_sqfs(ret);
		goto out;
	}

	if (!is_real_dir)
		underlying_dir = create_backing_dir();
	if (underlying_dir == NULL)
		goto out;

	struct DirMapEntry *dir_entry = malloc(sizeof(struct DirMapEntry));
	if (dir_entry == NULL) {
		errno = ENOMEM;
		goto out;
	}

	dir_entry->key = underlying_dir;
	dir_entry->dir_reader = dir_reader;
	dir_entry->is_real_dir = is_real_dir;
	dir_entry->dir_entry_list = NULL;
	dir_entry->dir_entry_current = NULL;
	dir_entry->dir_entry_list_reached_end = false;
	HASH_ADD_PTR(g_xSqfsMgr.dir_map, key, dir_entry);

	enumerate_dir(dir_reader, &dir_entry->dir_entry_list);

	sqfs_free(inode);
	result = underlying_dir;
	goto out_unlock_only;

out:
	int saved_errno = errno;

	sqfs_free(inode);
	sqfs_destroy(dir_reader);

	errno = saved_errno;

out_unlock_only:
	saved_errno = errno;
	sqfs_unlock();
	if (underlying_dir != NULL && result == NULL) {
		g_xLibcFuncs.closedir(underlying_dir);
		errno = saved_errno;
	}
	return result;
}

static void dump_dirent(const char* prefix, const struct dirent *entry) {
	if (entry == NULL) {
		log_msg("  %s (null)\n", prefix);
		return;
	}

	log_msg("  %s ino=%llu off=%lld reclen=%u type=%u name=\"%s\"\n",
		prefix,
		(unsigned long long)entry->d_ino,
		(long long)entry->d_off,
		(unsigned int)entry->d_reclen,
		(unsigned int)entry->d_type,
		entry ? entry->d_name : "(null)");
}

struct dirent *sqfs_readdir(DIR *dirp) {
	struct DirMapEntry *found = NULL;
	bool log_readdir = log_enabled(__func__);

	sqfs_lock();
	HASH_FIND_PTR(g_xSqfsMgr.dir_map, &dirp, found);

	// If the given dirp is not in our map,
	// it must be a real directory that sqfs_opendir didn't open, fallback.
	if (found == NULL) {
		sqfs_unlock();
		return g_xLibcFuncs.readdir(dirp);
	}

	if (found->is_real_dir) {
		struct dirent *result = g_xLibcFuncs.readdir(dirp);
		if (result != NULL) {
			if (log_readdir)
				dump_dirent("real_dir:", result);
			sqfs_unlock();
			return result;
		}
	}

	if (found->dir_entry_list_reached_end) {
		if (log_readdir)
			dump_dirent("sqfs_dir:", NULL);
		sqfs_unlock();
		return NULL;
	}

	if (found->dir_entry_current == NULL)
		found->dir_entry_current = found->dir_entry_list;

	if (found->dir_entry_current == NULL) {
		sqfs_unlock();
		return NULL;
	}

	struct dirent *result = &found->dir_entry_current->dirent;
	found->dir_entry_current = found->dir_entry_current->next;

	if (found->dir_entry_current == NULL)
		found->dir_entry_list_reached_end = true;

	if (log_readdir)
		dump_dirent("sqfs_dir:", result);

	sqfs_unlock();
	return result;
}

int sqfs_closedir(DIR *dir) {
	struct DirMapEntry *found = NULL;

	sqfs_lock();
	HASH_FIND_PTR(g_xSqfsMgr.dir_map, &dir, found);

	// If the given dirp is not in our map,
	// it must be a real directory that sqfs_opendir didn't open, fallback.
	if (found == NULL) {
		sqfs_unlock();
		return g_xLibcFuncs.closedir(dir);
	}

	HASH_DEL(g_xSqfsMgr.dir_map, found);
	sqfs_destroy(found->dir_reader);
	DIR* underlying_dir = found->key;
	free_sqfs_dir_entry_list(&found->dir_entry_list);
	free(found);

	log_hook(__func__, "dir=%p\n", dir);

	sqfs_unlock();
	return g_xLibcFuncs.closedir(underlying_dir);
}






static void bytes_to_hex(const sqfs_u8 *in, size_t count, char *out)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < count; i++) {
		out[i * 2] = hex[in[i] >> 4];
		out[i * 2 + 1] = hex[in[i] & 0x0F];
	}

	out[count * 2] = '\0';
}

void test_sqfs_listing(void) {
	sqfs_dir_reader_t *dir = NULL;
	sqfs_inode_generic_t *root = NULL;
	int ret;

	sqfs_mgr_load_image();

	sqfs_lock();
	if (!sqfs_mgr_loaded_locked())
		goto out;

	dir = sqfs_dir_reader_create(
		&g_xSqfsMgr.super, g_xSqfsMgr.compressor, g_xSqfsMgr.file, 0);
	if (dir == NULL) {
		log_msg("sqfs_dir_reader_create failed\n");
		goto out;
	}

	ret = sqfs_dir_reader_get_root_inode(dir, &root);
	if (ret != 0) {
		log_msg("sqfs_dir_reader_get_root_inode failed: %d\n", ret);
		goto out;
	}

	ret = sqfs_dir_reader_open_dir(dir, root, 0);
	if (ret != 0) {
		log_msg("sqfs_dir_reader_open_dir failed: %d\n", ret);
		goto out;
	}

	log_msg("/ files:\n");
	for (;;) {
		sqfs_dir_entry_t *entry = NULL;
		sqfs_inode_generic_t *inode = NULL;
		sqfs_u64 size = 0;
		sqfs_u8 head[16];
		char hex[sizeof(head) * 2 + 1];
		sqfs_u32 want;
		sqfs_s32 got;

		ret = sqfs_dir_reader_read(dir, &entry);
		if (ret > 0)
			break;
		if (ret < 0) {
			log_msg("sqfs_dir_reader_read failed: %d\n", ret);
			break;
		}

		if (entry->type != SQFS_INODE_FILE &&
		    entry->type != SQFS_INODE_EXT_FILE) {
			sqfs_free(entry);
			continue;
		}

		ret = sqfs_dir_reader_get_inode(dir, &inode);
		if (ret != 0) {
			log_msg("  %.*s size=? head16=<inode error %d>\n",
				(int)entry->size + 1, entry->name, ret);
			sqfs_free(entry);
			continue;
		}

		ret = sqfs_inode_get_file_size(inode, &size);
		if (ret != 0) {
			log_msg("  %.*s size=? head16=<size error %d>\n",
				(int)entry->size + 1, entry->name, ret);
			sqfs_free(inode);
			sqfs_free(entry);
			continue;
		}

		want = size < sizeof(head) ? (sqfs_u32)size : (sqfs_u32)sizeof(head);
		got = want > 0 ? sqfs_data_reader_read(g_xSqfsMgr.data_reader, inode, 0, head, want) : 0;
		if (got < 0) {
			log_msg("  %.*s size=%llu head16=<read error %d>\n",
				(int)entry->size + 1, entry->name,
				(unsigned long long)size, (int)got);
			sqfs_free(inode);
			sqfs_free(entry);
			continue;
		}

		bytes_to_hex(head, (size_t)got, hex);
		log_msg("  %.*s size=%llu head16=%s\n",
			(int)entry->size + 1, entry->name,
			(unsigned long long)size, hex);
		sqfs_free(inode);
		sqfs_free(entry);
	}

out:
	sqfs_free(root);
	sqfs_destroy(dir);
	sqfs_mgr_unload_image();
	sqfs_unlock();
}
