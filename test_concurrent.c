#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_THREADS 8
#define DEFAULT_ITERS 32
#define PREFIX_LIMIT (1024 * 1024)
#define SAMPLE_COUNT 128
#define SAMPLE_CHUNK 4096
#define READ_CHUNK 8192

static const uint64_t FNV_OFFSET = UINT64_C(1469598103934665603);
static const uint64_t FNV_PRIME = UINT64_C(1099511628211);

struct TestConfig {
	const char *path;
	off_t size;
	size_t prefix_size;
	uint64_t prefix_hash;
	uint64_t sample_hash;
	int iterations;
};

struct SharedReadCtx {
	int fd;
	unsigned long long total;
	int errors;
	pthread_mutex_t lock;
};

struct WorkerCtx {
	const struct TestConfig *cfg;
	int index;
};

extern int __xstat(int ver, const char *path, struct stat *buf);

typedef ssize_t (*pread_func_t)(int fd, void *buf, size_t count, off_t offset);
typedef ssize_t (*pread64_func_t)(int fd, void *buf, size_t count,
				  off64_t offset);

static pread_func_t volatile g_pread_func = pread;
static pread64_func_t volatile g_pread64_func = pread64;
static pthread_mutex_t g_print_lock = PTHREAD_MUTEX_INITIALIZER;

static void print_error(const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&g_print_lock);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&g_print_lock);
}

static void hash_update(uint64_t *hash, const void *data, size_t len)
{
	const unsigned char *p = data;

	for (size_t i = 0; i < len; i++) {
		*hash ^= p[i];
		*hash *= FNV_PRIME;
	}
}

static size_t min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static off64_t sample_offset(off_t size, int index)
{
	uint64_t mixed;

	if (size <= 0)
		return 0;

	mixed = (uint64_t)(unsigned int)index * UINT64_C(1103515245) +
		UINT64_C(12345);
	return (off64_t)(mixed % (uint64_t)size);
}

static ssize_t call_pread_variant(int fd, void *buf, size_t count,
				  off64_t offset, bool use_pread64)
{
	if (use_pread64)
		return g_pread64_func(fd, buf, count, offset);

	return g_pread_func(fd, buf, count, (off_t)offset);
}

static int get_path_size(const char *path, off_t *size_out)
{
	int fd;
	off_t size;
	int saved_errno;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	size = lseek(fd, 0, SEEK_END);
	saved_errno = errno;
	if (close(fd) != 0 && size >= 0)
		return -1;

	if (size < 0) {
		errno = saved_errno;
		return -1;
	}

	*size_out = size;
	return 0;
}

static int hash_prefix_with_read(const char *path, size_t prefix_size,
				 uint64_t *hash_out)
{
	unsigned char buf[READ_CHUNK];
	size_t total = 0;
	uint64_t hash = FNV_OFFSET;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	while (total < prefix_size) {
		size_t want = min_size(sizeof(buf), prefix_size - total);
		ssize_t got = read(fd, buf, want);

		if (got < 0) {
			int saved_errno = errno;
			close(fd);
			errno = saved_errno;
			return -1;
		}
		if (got == 0)
			break;

		hash_update(&hash, buf, (size_t)got);
		total += (size_t)got;
	}

	if (close(fd) != 0)
		return -1;

	if (total != prefix_size) {
		errno = EIO;
		return -1;
	}

	*hash_out = hash;
	return 0;
}

static int hash_samples_with_pread(const char *path, off_t size,
				   bool use_pread64, uint64_t *hash_out)
{
	unsigned char buf[SAMPLE_CHUNK];
	uint64_t hash = FNV_OFFSET;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	for (int i = 0; i < SAMPLE_COUNT; i++) {
		off64_t offset = sample_offset(size, i);
		size_t want;
		size_t done = 0;

		if (size <= 0)
			break;

		want = min_size(sizeof(buf), (size_t)((off64_t)size - offset));
		while (done < want) {
			ssize_t got = call_pread_variant(fd, buf + done,
							 want - done,
							 offset + (off64_t)done,
							 use_pread64);
			if (got < 0) {
				int saved_errno = errno;
				close(fd);
				errno = saved_errno;
				return -1;
			}
			if (got == 0) {
				close(fd);
				errno = EIO;
				return -1;
			}

			done += (size_t)got;
		}

		hash_update(&hash, buf, done);
	}

	if (close(fd) != 0)
		return -1;

	*hash_out = hash;
	return 0;
}

static int probe_lseek(const struct TestConfig *cfg)
{
	unsigned char via_read[256];
	unsigned char via_pread[sizeof(via_read)];
	off_t offset;
	size_t want;
	ssize_t got_read;
	ssize_t got_pread;
	int fd;

	if (cfg->size <= 0)
		return 0;

	offset = cfg->size / 2;
	want = min_size(sizeof(via_read), (size_t)(cfg->size - offset));

	fd = open(cfg->path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	if (lseek(fd, offset, SEEK_SET) != offset) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}

	got_read = read(fd, via_read, want);
	if (got_read < 0) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}

	got_pread = call_pread_variant(fd, via_pread, want, (off64_t)offset,
				       true);
	if (got_pread < 0) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}

	if (close(fd) != 0)
		return -1;

	if (got_read != got_pread ||
	    memcmp(via_read, via_pread, (size_t)got_read) != 0) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static int probe_xstat_access(const struct TestConfig *cfg)
{
	struct stat st;

	if (__xstat(3, cfg->path, &st) != 0)
		return -1;

	if (st.st_size != cfg->size) {
		errno = EIO;
		return -1;
	}

	if (access(cfg->path, R_OK) != 0)
		return -1;

	return 0;
}

static void *worker_main(void *arg)
{
	const struct WorkerCtx *worker = arg;
	const struct TestConfig *cfg = worker->cfg;
	int errors = 0;

	for (int i = 0; i < cfg->iterations; i++) {
		uint64_t hash;
		bool use_pread64 = (i % 2) != 0;

		if (hash_prefix_with_read(cfg->path, cfg->prefix_size, &hash) != 0) {
			print_error("worker %d: read prefix failed at iter %d: %s\n",
				    worker->index, i, strerror(errno));
			errors++;
		} else if (hash != cfg->prefix_hash) {
			print_error("worker %d: read prefix hash mismatch at iter %d\n",
				    worker->index, i);
			errors++;
		}

		if (hash_samples_with_pread(cfg->path, cfg->size,
					    use_pread64, &hash) != 0) {
			print_error("worker %d: %s samples failed at iter %d: %s\n",
				    worker->index,
				    use_pread64 ? "pread64" : "pread",
				    i, strerror(errno));
			errors++;
		} else if (hash != cfg->sample_hash) {
			print_error("worker %d: sample hash mismatch at iter %d\n",
				    worker->index, i);
			errors++;
		}

		if (probe_lseek(cfg) != 0) {
			print_error("worker %d: lseek probe failed at iter %d: %s\n",
				    worker->index, i, strerror(errno));
			errors++;
		}

		if (probe_xstat_access(cfg) != 0) {
			print_error("worker %d: xstat/access probe failed at iter %d: %s\n",
				    worker->index, i, strerror(errno));
			errors++;
		}
	}

	return (void *)(uintptr_t)errors;
}

static void *shared_read_main(void *arg)
{
	struct SharedReadCtx *ctx = arg;
	unsigned char buf[READ_CHUNK];

	for (;;) {
		ssize_t got = read(ctx->fd, buf, sizeof(buf));

		if (got < 0) {
			pthread_mutex_lock(&ctx->lock);
			ctx->errors++;
			pthread_mutex_unlock(&ctx->lock);
			return NULL;
		}
		if (got == 0)
			return NULL;

		pthread_mutex_lock(&ctx->lock);
		ctx->total += (unsigned long long)got;
		pthread_mutex_unlock(&ctx->lock);
	}
}

static int run_independent_workers(const struct TestConfig *cfg, int threads)
{
	pthread_t *thread_ids = calloc((size_t)threads, sizeof(*thread_ids));
	struct WorkerCtx *workers = calloc((size_t)threads, sizeof(*workers));
	int errors = 0;

	if (thread_ids == NULL || workers == NULL) {
		free(thread_ids);
		free(workers);
		errno = ENOMEM;
		return -1;
	}

	for (int i = 0; i < threads; i++) {
		workers[i].cfg = cfg;
		workers[i].index = i;
		if (pthread_create(&thread_ids[i], NULL, worker_main,
				   &workers[i]) != 0) {
			threads = i;
			errors++;
			break;
		}
	}

	for (int i = 0; i < threads; i++) {
		void *ret = NULL;
		pthread_join(thread_ids[i], &ret);
		errors += (int)(uintptr_t)ret;
	}

	free(thread_ids);
	free(workers);

	return errors == 0 ? 0 : -1;
}

static int run_shared_fd_read(const struct TestConfig *cfg, int threads)
{
	pthread_t *thread_ids = calloc((size_t)threads, sizeof(*thread_ids));
	struct SharedReadCtx ctx = {
		.fd = -1,
		.total = 0,
		.errors = 0,
		.lock = PTHREAD_MUTEX_INITIALIZER,
	};
	int started = 0;

	if (thread_ids == NULL) {
		errno = ENOMEM;
		return -1;
	}

	ctx.fd = open(cfg->path, O_RDONLY | O_CLOEXEC);
	if (ctx.fd < 0) {
		free(thread_ids);
		return -1;
	}

	if (lseek(ctx.fd, 0, SEEK_SET) != 0) {
		int saved_errno = errno;
		close(ctx.fd);
		free(thread_ids);
		errno = saved_errno;
		return -1;
	}

	for (int i = 0; i < threads; i++) {
		if (pthread_create(&thread_ids[i], NULL, shared_read_main,
				   &ctx) != 0)
			break;
		started++;
	}

	for (int i = 0; i < started; i++)
		pthread_join(thread_ids[i], NULL);

	if (close(ctx.fd) != 0) {
		free(thread_ids);
		return -1;
	}

	free(thread_ids);

	if (started != threads || ctx.errors != 0 ||
	    ctx.total != (unsigned long long)cfg->size) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static int parse_positive(const char *s, int fallback)
{
	char *end = NULL;
	long value;

	if (s == NULL)
		return fallback;

	value = strtol(s, &end, 10);
	if (end == s || *end != '\0' || value <= 0 || value > 1024)
		return fallback;

	return (int)value;
}

int main(int argc, char **argv)
{
	struct TestConfig cfg = {0};
	int threads = DEFAULT_THREADS;
	int errors = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path> [threads] [iterations]\n", argv[0]);
		return 2;
	}

	cfg.path = argv[1];
	threads = argc > 2 ? parse_positive(argv[2], DEFAULT_THREADS) :
		parse_positive(getenv("HOOKSQFS_TEST_THREADS"), DEFAULT_THREADS);
	cfg.iterations = argc > 3 ? parse_positive(argv[3], DEFAULT_ITERS) :
		parse_positive(getenv("HOOKSQFS_TEST_ITERS"), DEFAULT_ITERS);

	if (get_path_size(cfg.path, &cfg.size) != 0) {
		fprintf(stderr, "size probe for %s failed: %s\n",
			cfg.path, strerror(errno));
		return 1;
	}

	cfg.prefix_size = min_size((size_t)cfg.size, PREFIX_LIMIT);

	if (hash_prefix_with_read(cfg.path, cfg.prefix_size,
				  &cfg.prefix_hash) != 0) {
		fprintf(stderr, "baseline read failed: %s\n", strerror(errno));
		return 1;
	}

	if (hash_samples_with_pread(cfg.path, cfg.size, true,
				    &cfg.sample_hash) != 0) {
		fprintf(stderr, "baseline pread64 failed: %s\n", strerror(errno));
		return 1;
	}

	fprintf(stderr,
		"concurrent test: path=%s size=%lld threads=%d iterations=%d\n",
		cfg.path, (long long)cfg.size, threads, cfg.iterations);

	if (run_independent_workers(&cfg, threads) != 0)
		errors++;

	if (run_shared_fd_read(&cfg, threads) != 0) {
		fprintf(stderr, "shared fd read failed: %s\n", strerror(errno));
		errors++;
	}

	if (errors != 0)
		return 1;

	fprintf(stderr, "concurrent test OK\n");
	return 0;
}
