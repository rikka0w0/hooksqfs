#define _GNU_SOURCE

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "funchook.h"
#include "logging.h"
#include "sqfs_mgr.h"
#include "real.h"

static funchook_t *hooks_funchook;

/* ------------------------------------------------------------------ */
/* open / open64                                                       */
/* ------------------------------------------------------------------ */
/*
static int flags_need_mode(int flags)
{
#ifdef O_TMPFILE
	if ((flags & O_TMPFILE) == O_TMPFILE)
		return 1;
#endif
	return (flags & O_CREAT) != 0;
}

int open(const char *pathname, int flags, ...)
{
	mode_t mode = 0;
	int has_mode = flags_need_mode(flags);

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

		int sqfs_fd = sqfs_open(pathname, flags, mode);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o, sqfs_fd=%d\n",
					pathname , flags, mode, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o\n",
				pathname , flags, mode);

		return g_LibcFuncs.open(pathname, flags, mode);
	} else {
		int sqfs_fd = sqfs_open(pathname, flags);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, sqfs_fd=%d\n",
					pathname , flags, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x\n",
				pathname , flags);

		return g_LibcFuncs.open(pathname, flags);
	}
}

int open64(const char *pathname, int flags, ...)
{
	mode_t mode = 0;
	int has_mode = flags_need_mode(flags);

	if (g_LibcFuncs.open64 == NULL) {
		errno = ENOSYS;
		return -1;
	}

	if (has_mode) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);

		int sqfs_fd = sqfs_open(pathname, flags, mode);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o, sqfs_fd=%d\n",
					pathname , flags, mode, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x, mode=%04o\n",
				pathname , flags, mode);

		return g_LibcFuncs.open64(pathname, flags, mode);
	} else {
		int sqfs_fd = sqfs_open(pathname, flags);
		if (sqfs_fd >= 0) {
			log_hook(__func__, "path=\"%s\", flags=0x%x, sqfs_fd=%d\n",
					pathname , flags, sqfs_fd);
			return sqfs_fd;
		}
		if (errno != ENOENT)
			return -1;

		log_hook(__func__, "path=\"%s\", flags=0x%x\n",
				pathname , flags);

		return g_LibcFuncs.open64(pathname, flags);
	}
}
*/

/* ------------------------------------------------------------------ */
/* scandir                                                            */
/* ------------------------------------------------------------------ */

/*
int scandir(const char *dirp, struct dirent ***namelist,
	    int (*filter)(const struct dirent *),
	    int (*compar)(const struct dirent **, const struct dirent **))
{
	int ret = g_LibcFuncs.scandir(dirp, namelist, filter, compar);
	int saved_errno = errno;

	log_msg("scandir: path=\"%s\", ret=%d\n", dirp, ret);
	if (ret > 0 && *namelist != NULL) {
		for (int i = 0; i < ret; i++) {
			struct dirent *entry = (*namelist)[i];

			log_msg("scandir: [%d] name=\"%s\", type=%u, ino=%llu\n",
				i,
				entry ? entry->d_name : "(null)",
				entry ? (unsigned int)entry->d_type : 0,
				entry ? (unsigned long long)entry->d_ino : 0);
		}
	} else if (ret == 0) {
		log_msg("scandir: <empty>\n");
	} else {
		log_msg("scandir: <unavailable>\n");
	}

	errno = saved_errno;
	return ret;
}
*/

/* ------------------------------------------------------------------ */
/* funchook install / uninstall                                       */
/* ------------------------------------------------------------------ */

static int prepare_hook(const char *name, void **target, void *hook)
{
	int rv = funchook_prepare(hooks_funchook, target, hook);
	if (rv != 0) {
		log_msg("funchook_prepare(%s) failed: %s\n",
			name, funchook_error_message(hooks_funchook));
		return -1;
	}

	return 0;
}

static void uninstall_hooks(void)
{
	int rv;

	if (hooks_funchook == NULL)
		return;

	rv = funchook_uninstall(hooks_funchook, 0);
	if (rv != 0) {
		log_msg("funchook_uninstall failed: %s\n",
			funchook_error_message(hooks_funchook));
	}

	rv = funchook_destroy(hooks_funchook);
	if (rv != 0)
		log_msg("funchook_destroy failed\n");

	hooks_funchook = NULL;
	g_LibcFuncs.open = g_xTrampoline.open;
	g_LibcFuncs.read = g_xTrampoline.read;
	g_LibcFuncs.pread = g_xTrampoline.pread;
	g_LibcFuncs.pread64 = g_xTrampoline.pread64;
	g_LibcFuncs.lseek = g_xTrampoline.lseek;
	g_LibcFuncs.lseek64 = g_xTrampoline.lseek64;
	g_LibcFuncs.close = g_xTrampoline.close;
	g_LibcFuncs.__close_nocancel = g_xTrampoline.__close_nocancel;
	g_LibcFuncs.opendir = g_xTrampoline.opendir;
	g_LibcFuncs.readdir = g_xTrampoline.readdir;
	g_LibcFuncs.closedir = g_xTrampoline.closedir;
	g_LibcFuncs.access = g_xTrampoline.access;
	g_LibcFuncs.__xstat = g_xTrampoline.__xstat;
}

static void install_hooks(void)
{
	int rv;

	if (g_LibcFuncs.open == NULL || g_LibcFuncs.read == NULL ||
	    g_LibcFuncs.pread == NULL || g_LibcFuncs.lseek == NULL ||
	    g_LibcFuncs.close == NULL ||
	    g_LibcFuncs.opendir == NULL || g_LibcFuncs.readdir == NULL ||
	    g_LibcFuncs.closedir == NULL || g_LibcFuncs.access == NULL ||
	    g_LibcFuncs.__xstat == NULL) {
		log_msg("hook install failed: missing real function\n");
		return;
	}

	g_xTrampoline.open = g_LibcFuncs.open;
	g_xTrampoline.read = g_LibcFuncs.read;
	g_xTrampoline.pread = g_LibcFuncs.pread;
	g_xTrampoline.pread64 = g_LibcFuncs.pread64;
	g_xTrampoline.lseek = g_LibcFuncs.lseek;
	g_xTrampoline.lseek64 = g_LibcFuncs.lseek64;
	g_xTrampoline.close = g_LibcFuncs.close;
	g_xTrampoline.__close_nocancel = g_LibcFuncs.__close_nocancel;
	g_xTrampoline.opendir = g_LibcFuncs.opendir;
	g_xTrampoline.readdir = g_LibcFuncs.readdir;
	g_xTrampoline.closedir = g_LibcFuncs.closedir;
	g_xTrampoline.access = g_LibcFuncs.access;
	g_xTrampoline.__xstat = g_LibcFuncs.__xstat;

	hooks_funchook = funchook_create();
	if (hooks_funchook == NULL) {
		log_msg("funchook_create failed\n");
		return;
	}

	if (prepare_hook("open", (void **)&g_LibcFuncs.open, (void *)sqfs_open) != 0 ||
	    prepare_hook("read", (void **)&g_LibcFuncs.read, (void *)sqfs_read) != 0 ||
	    prepare_hook("pread", (void **)&g_LibcFuncs.pread, (void *)sqfs_pread) != 0 ||
	    prepare_hook("lseek", (void **)&g_LibcFuncs.lseek, (void *)sqfs_lseek) != 0 ||
	    ((void *)g_LibcFuncs.pread64 != (void *)g_LibcFuncs.pread &&
	     prepare_hook("pread64", (void **)&g_LibcFuncs.pread64,
			  (void *)sqfs_pread64) != 0) ||
	    ((void *)g_LibcFuncs.lseek64 != (void *)g_LibcFuncs.lseek &&
	     prepare_hook("lseek64", (void **)&g_LibcFuncs.lseek64,
			  (void *)sqfs_lseek64) != 0) ||
	    prepare_hook("close", (void **)&g_LibcFuncs.close, (void *)sqfs_close) != 0 ||
	    (g_xTrampoline.__close_nocancel != NULL &&
	     (void *)g_xTrampoline.__close_nocancel != (void *)g_xTrampoline.close &&
	     prepare_hook("__close_nocancel", (void **)&g_LibcFuncs.__close_nocancel,
			  (void *)sqfs_close_nocancel) != 0) ||
	    prepare_hook("opendir", (void **)&g_LibcFuncs.opendir, (void *)sqfs_opendir) != 0 ||
	    prepare_hook("readdir", (void **)&g_LibcFuncs.readdir, (void *)sqfs_readdir) != 0 ||
	    prepare_hook("closedir", (void **)&g_LibcFuncs.closedir, (void *)sqfs_closedir) != 0 ||
	    prepare_hook("access", (void **)&g_LibcFuncs.access, (void *)sqfs_access) != 0 ||
	    prepare_hook("__xstat", (void **)&g_LibcFuncs.__xstat, (void *)sqfs_xstat) != 0) {
		uninstall_hooks();
		return;
	}

	rv = funchook_install(hooks_funchook, 0);
	if (rv != 0) {
		log_msg("funchook_install failed: %s\n",
			funchook_error_message(hooks_funchook));
		uninstall_hooks();
		return;
	}

	log_msg("hooks installed:\n");
	log_msg("  open=%p\n", (void *)g_LibcFuncs.open);
	log_msg("  read=%p\n", (void *)g_LibcFuncs.read);
	log_msg("  pread=%p\n", (void *)g_LibcFuncs.pread);
	log_msg("  pread64=%p\n", (void *)g_LibcFuncs.pread64);
	log_msg("  lseek=%p\n", (void *)g_LibcFuncs.lseek);
	log_msg("  lseek64=%p\n", (void *)g_LibcFuncs.lseek64);
	log_msg("  close=%p\n", (void *)g_LibcFuncs.close);
	log_msg("  __close_nocancel=%p\n", (void *)g_LibcFuncs.__close_nocancel);
	log_msg("  opendir=%p\n", (void *)g_LibcFuncs.opendir);
	log_msg("  readdir=%p\n", (void *)g_LibcFuncs.readdir);
	log_msg("  closedir=%p\n", (void *)g_LibcFuncs.closedir);
	log_msg("  access=%p\n", (void *)g_LibcFuncs.access);
	log_msg("  __xstat=%p\n", (void *)g_LibcFuncs.__xstat);
}

/* ------------------------------------------------------------------ */
/* so constructor / destructor                                        */
/* ------------------------------------------------------------------ */

__attribute__((constructor))
static void hooksqfs_init(void)
{
	// This has to be the first step
	vPopulateLibcFuncPtrs();

	sqfs_mgr_load_image();

	// This has to be the last step
	install_hooks();
}

__attribute__((destructor))
static void hooksqfs_fini(void)
{
	uninstall_hooks();
}
