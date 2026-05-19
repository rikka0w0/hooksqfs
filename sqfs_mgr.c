#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>

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

void sqfs_util_log_failure(const char* func, int err);

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

	struct FDMapEntry *fd_entry = malloc(sizeof(struct FDMapEntry));
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
		node->dirent.d_off = 0; // Not used
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

DIR * sqfs_opendir(const char *name) {
	const char *prefix = sqfs_mgr_get_prefix();
	const char *sqfs_path = getenv("HOOKSQFS_FILE");
	char relative[PATH_MAX];

	if (name == NULL) {
		errno = EFAULT;
		return NULL;
	}

	if (!path_is_under_prefix(prefix, name)) {
		errno = ENOENT;
		return NULL;
	}

	if (path_equals_normalized(name, sqfs_path)) {
		errno = ENOENT;
		return NULL;
	}

	if (!path_relative_to_root(prefix, name, relative, sizeof(relative))) {
		errno = ENOENT;
		return NULL;
	}

	log_msg("sqfs_opendir: %s -> %s\n", name, relative);

	if (!sqfs_mgr_load_image()) {
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
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_dir_reader_find_by_path", ret);
		log_hook(__func__, "sqfs_dir_reader_find_by_path(\"%s\") failed: %d\n", relative, ret);
		errno = errno_from_sqfs(ret);
		goto out;
	}

	if (inode->base.type != SQFS_INODE_DIR &&
	       inode->base.type != SQFS_INODE_EXT_DIR) {
		errno = ENOTDIR;
		goto out;
	}

	ret = sqfs_dir_reader_open_dir(dir_reader, inode, 0);
	if (ret != 0) {
		sqfs_util_log_failure("sqfs_dir_reader_open_dir", ret);
		errno = errno_from_sqfs(ret);
		goto out;
	}

	bool is_real_dir = true;
	DIR *underlying_dir = real.opendir(name);
	if (underlying_dir == NULL) {
		underlying_dir = create_backing_dir();
		is_real_dir = false;
	}
	if (underlying_dir == NULL)
		goto out;

	struct DirMapEntry *dir_entry = malloc(sizeof(struct DirMapEntry));
	if (dir_entry == NULL) {
		errno = ENOMEM;
		real.closedir(underlying_dir);
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
	return underlying_dir;

out:
	int saved_errno = errno;

	sqfs_free(inode);
	sqfs_destroy(dir_reader);

	errno = saved_errno;
	return NULL;
}

struct dirent *sqfs_readdir(DIR *dirp) {
	struct DirMapEntry *found = NULL;

	HASH_FIND_PTR(g_xSqfsMgr.dir_map, &dirp, found);

	// If the given dirp is not in our map,
	// it must be a real directory that sqfs_opendir didn't open, return.
	if (found == NULL)
		return NULL;

	if (found->is_real_dir) {
		struct dirent *result = real.readdir(dirp);
		if (result != NULL)
			return result;
	}

	if (found->dir_entry_list_reached_end)
		return NULL;

	if (found->dir_entry_current == NULL)
		found->dir_entry_current = found->dir_entry_list;

	if (found->dir_entry_current == NULL)
		return NULL;

	struct dirent *result = &found->dir_entry_current->dirent;
	found->dir_entry_current = found->dir_entry_current->next;

	if (found->dir_entry_current == NULL)
		found->dir_entry_list_reached_end = true;

	log_msg(" inode %d -> %s\n", result->d_ino, result->d_name);
	return result;
}

int sqfs_closedir(DIR *dir) {
	struct DirMapEntry *found = NULL;

	HASH_FIND_PTR(g_xSqfsMgr.dir_map, &dir, found);
	if (found == NULL)
		return -1;

	HASH_DEL(g_xSqfsMgr.dir_map, found);
	sqfs_destroy(found->dir_reader);
	DIR* underlying_dir = found->key;
	free_sqfs_dir_entry_list(&found->dir_entry_list);
	free(found);

	return real.closedir(underlying_dir);
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
