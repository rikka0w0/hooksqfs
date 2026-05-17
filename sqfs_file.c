#include <stdlib.h>
#include <sqfs/compressor.h>
#include <sqfs/dir.h>
#include <sqfs/dir_reader.h>
#include <sqfs/super.h>
#include <sqfs/io.h>

#include "logging.h"

static int test_sqfs_file_initialized = 0;

static const char *sqfs_compressor_name_readable(SQFS_COMPRESSOR id)
{
	const char *name = sqfs_compressor_name_from_id(id);
	return name != NULL ? name : "unknown";
}

static void sqfs_mgr_log_root_dir(sqfs_file_t *file, const sqfs_super_t *super)
{
	sqfs_compressor_config_t cfg;
	sqfs_compressor_t *cmp = NULL;
	sqfs_dir_reader_t *dir = NULL;
	sqfs_inode_generic_t *root = NULL;
	int ret;

	ret = sqfs_compressor_config_init(&cfg, super->compression_id,
					  super->block_size,
					  SQFS_COMP_FLAG_UNCOMPRESS);
	if (ret != 0) {
		log_msg("sqfs_compressor_config_init failed: %d\n", ret);
		return;
	}

	ret = sqfs_compressor_create(&cfg, &cmp);
	if (ret != 0) {
		log_msg("sqfs_compressor_create failed: %d\n", ret);
		return;
	}

	if (super->flags & SQFS_FLAG_COMPRESSOR_OPTIONS) {
		ret = cmp->read_options(cmp, file);
		if (ret != 0) {
			log_msg("compressor read_options failed: %d\n", ret);
			goto out;
		}
	}

	dir = sqfs_dir_reader_create(super, cmp, file, 0);
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

	log_msg("/ directory entries:\n");
	for (;;) {
		sqfs_dir_entry_t *entry = NULL;

		ret = sqfs_dir_reader_read(dir, &entry);
		if (ret > 0)
			break;
		if (ret < 0) {
			log_msg("sqfs_dir_reader_read failed: %d\n", ret);
			break;
		}

		log_msg("  %.*s\n", (int)entry->size + 1, entry->name);
		sqfs_free(entry);
	}

out:
	sqfs_free(root);
	sqfs_destroy(dir);
	sqfs_destroy(cmp);
}

void sqfs_mgr_load_file(void)
{
	if (test_sqfs_file_initialized)
		return;
	test_sqfs_file_initialized = 1;

	const char *sqfs_path = getenv("HOOKSQFS_FILE");
	if (!sqfs_path) {
		log_msg("HOOKSQFS_FILE not set\n");
		return;
	}

	sqfs_file_t *file = sqfs_open_file(sqfs_path, SQFS_FILE_OPEN_READ_ONLY);

	if (file == NULL) {
		log_msg("sqfs_open_file failed\n");
		return;
	}

	sqfs_super_t super;
	int ret = sqfs_super_read(&super, file);
	if (ret != 0) {
		log_msg("sqfs_super_read failed: %d\n", ret);
		sqfs_free(file);
		return;
	}

	log_msg("libsquashfs read OK\n");
	log_msg("inodes: %u\n", super.inode_count);
	log_msg("block size: %u\n", super.block_size);
	log_msg("bytes used: %llu\n", (unsigned long long)super.bytes_used);
	log_msg("compression: %s\n",
		sqfs_compressor_name_readable(super.compression_id));
	sqfs_mgr_log_root_dir(file, &super);

	sqfs_free(file);
}

int main(void)
{
	sqfs_mgr_load_file();
	return 0;
}
