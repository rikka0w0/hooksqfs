#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

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

#include "uthash.h"

void sqfs_util_log_failure(const char* func, int err);

struct DirMapEntry {
	void *key;
	UT_hash_handle hh;
};

struct FDMapEntry {
	int key; // The underlying file descriptor.
	UT_hash_handle hh;

	// The corresponding squashfs inode
	sqfs_inode_generic_t *inode;
};

static struct sqfs_mgr {
	// Do not inteprete the other fields if field file is NULL.
	sqfs_file_t *file;
	sqfs_compressor_t *compressor;
	sqfs_data_reader_t *data_reader;

	sqfs_super_t super;
	sqfs_compressor_config_t cfg;

	struct DirMapEntry *dir_map;
	struct FDMapEntry *fd_map;
} g_xSqfsMgr = {0};

static const char *sqfs_mgr_get_prefix(void)
{
	const char *prefix = getenv("HOOKSQFS_PREFIX");

	return prefix != NULL ? prefix : "/hook";
}

static bool open_flags_need_write(int flags)
{
	int accmode = flags & O_ACCMODE;

	return accmode == O_WRONLY || accmode == O_RDWR ||
	       (flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0;
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

void sqfs_util_log_failure(const char* func, int err)
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

void sqfs_mgr_unload_image(void)
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
	if (g_xSqfsMgr.file != NULL)
		return true;

	const char *sqfs_path = getenv("HOOKSQFS_FILE");
	if (!sqfs_path) {
		log_msg("HOOKSQFS_FILE not set\n");
		return false;
	}

	g_xSqfsMgr.file = sqfs_open_file(sqfs_path, SQFS_FILE_OPEN_READ_ONLY);
	if (g_xSqfsMgr.file == NULL) {
		log_msg("sqfs_open_file failed\n");
		return false;
	}

	int ret = sqfs_super_read(&g_xSqfsMgr.super, g_xSqfsMgr.file);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_super_read", ret);
		sqfs_mgr_unload_image();
		return false;
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
		return false;
	}

	ret = sqfs_compressor_create(&g_xSqfsMgr.cfg, &g_xSqfsMgr.compressor);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_compressor_create", ret);
		sqfs_mgr_unload_image();
		return false;
	}

	if (g_xSqfsMgr.super.flags & SQFS_FLAG_COMPRESSOR_OPTIONS) {
		ret = g_xSqfsMgr.compressor->read_options(g_xSqfsMgr.compressor, g_xSqfsMgr.file);
		if (ret != 0) {
			sqfs_util_log_failure("sqfs_compressor_read_options", ret);
			sqfs_mgr_unload_image();
			return false;
		}
	}

	g_xSqfsMgr.data_reader = sqfs_data_reader_create(
		g_xSqfsMgr.file, g_xSqfsMgr.super.block_size, g_xSqfsMgr.compressor, 0);
	if (g_xSqfsMgr.data_reader == NULL) {
		log_msg("sqfs_data_reader_create failed\n");
		sqfs_mgr_unload_image();
		return false;
	}

	ret = sqfs_data_reader_load_fragment_table(
		g_xSqfsMgr.data_reader, &g_xSqfsMgr.super);
	if (ret != 0) {
		log_msg("sqfs_data_reader_load_fragment_table failed: %d\n", ret);
		sqfs_mgr_unload_image();
		return false;
	}

	return true;
}

int sqfs_open(const char *pathname, int flags, ...) {
	const char *prefix = sqfs_mgr_get_prefix();
	const char *sqfs_path = getenv("HOOKSQFS_FILE");
	char relative[PATH_MAX];
	sqfs_u64 size = 0;
	int ret;

	if (pathname == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (!path_is_under_prefix(prefix, pathname)) {
		errno = ENOENT;
		return -1;
	}

	if (path_equals_normalized(pathname, sqfs_path)) {
		errno = ENOENT;
		return -1;
	}
	
	if (open_flags_need_write(flags)) {
		errno = EROFS;
		return -1;
	}

	if (!path_relative_to_root(prefix, pathname, relative, sizeof(relative))) {
		errno = ENOENT;
		return -1;
	}

	log_msg("sqfs_open: %s -> %s\n", pathname, relative);

	if (relative[0] == '\0') {
		errno = EISDIR;
		return -1;
	}

	if (!sqfs_mgr_load_image()) {
		errno = ENOENT;
		return -1;
	}

	sqfs_dir_reader_t *dir_reader = sqfs_dir_reader_create(
		&g_xSqfsMgr.super, g_xSqfsMgr.compressor, g_xSqfsMgr.file, 0);
	if (dir_reader == NULL) {
		errno = ENOMEM;
		return -1;
	}

	sqfs_inode_generic_t *inode = NULL;
	ret = sqfs_dir_reader_find_by_path(dir_reader, NULL, relative, &inode);
	sqfs_destroy(dir_reader);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_dir_reader_find_by_path", ret);
		errno = errno_from_sqfs(ret);
		return -1;
	}

	if (inode->base.type != SQFS_INODE_FILE &&
			inode->base.type != SQFS_INODE_EXT_FILE) {
		errno = EISDIR;
		goto error_and_free_inode;
	}

	ret = sqfs_inode_get_file_size(inode, &size);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_inode_get_file_size", ret);
		errno = errno_from_sqfs(ret);
		goto error_and_free_inode;
	}

	int fd = create_backing_fd(flags);
	if (fd < 0)
		goto error_and_free_inode;

	struct FDMapEntry *fd_entry = malloc(sizeof(*fd_entry));
	if (fd_entry == NULL) {
		errno = ENOMEM;
		syscall(SYS_close, fd);
		goto error_and_free_inode;
	}

	fd_entry->key = fd;
	fd_entry->inode = inode;
	HASH_ADD_PTR(g_xSqfsMgr.fd_map, key, fd_entry);
	return fd;

error_and_free_inode:
	sqfs_free(inode);
	return -1;
}

DIR * sqfs_opendir(const char *name) {
	// 首先用path_is_under_prefix判断下name是否在HOOKSQFS_PREFIX下
	// 如果不在就直接返回NULL。NULL的含义是这个name不能由sqfs_mgr处理。

	// 然后用path_relative_to_root处理下name，得到相对于根目录的路径

	return NULL; // Stub
}

int sqfs_closedir(DIR *dir) {
	struct DirMapEntry *found;
	// 首先在g_pxMapDIR里尝试以dir为key
	// 用HASH_FIND_PTR(g_xSqfsMgr.dir_map, &dir, found);
	// 找到dir对应的sqfs entry。
	// 如果找不到，说明这个dir不是sqfs_mgr打开的，直接返回-1， 不设置errno

	// 如果找到了，说明这个dir是sqfs_mgr打开的，继续调用sqfs_dir_reader_destroy销毁这个entry里的sqfs dir reader对象
	return 1;  // Stub
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
	sqfs_mgr_load_image();

	sqfs_dir_reader_t *dir = sqfs_dir_reader_create(
		&g_xSqfsMgr.super, g_xSqfsMgr.compressor, g_xSqfsMgr.file, 0);
	if (dir == NULL) {
		log_msg("sqfs_dir_reader_create failed\n");
		goto out;
	}

	sqfs_inode_generic_t * root = NULL;
	int ret = sqfs_dir_reader_get_root_inode(dir, &root);
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
}
