#include <stdlib.h>
#include <sqfs/super.h>
#include <sqfs/io.h>

#include "logging.h"

static int test_sqfs_file_initialized = 0;

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

	sqfs_free(file);
}