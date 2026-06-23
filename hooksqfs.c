#define _GNU_SOURCE

#include <stdbool.h>
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

#include "funchook.h"
#include "logging.h"
#include "sqfs_mgr.h"
#include "real.h"

/*
 * Current libc entry points used by hook implementations. Before hook
 * preparation these are RTLD_NEXT symbols; funchook_prepare rewrites prepared
 * entries to trampoline-callable originals.
 */
struct LibcFunctions g_xLibcFuncs = {0};

/*
 * Snapshot of the original libc entry points captured before installing hooks.
 * It is used to restore g_xLibcFuncs when hooks are uninstalled.
 */
struct LibcFunctions g_xTrampoline = {0};

static funchook_t *hooks_funchook;
static bool g_bLibcFuncsPopulated;

static void vPopulateLibcFuncPtrs(void)
{
	if (g_bLibcFuncsPopulated)
		return;

	g_xLibcFuncs.open = dlsym(RTLD_NEXT, "open");
	// g_xLibcFuncs.open64 = dlsym(RTLD_NEXT, "open64");
	g_xLibcFuncs.read = dlsym(RTLD_NEXT, "read");
	g_xLibcFuncs.pread = dlsym(RTLD_NEXT, "pread");
	g_xLibcFuncs.pread64 = dlsym(RTLD_NEXT, "pread64");
	g_xLibcFuncs.lseek = dlsym(RTLD_NEXT, "lseek");
	g_xLibcFuncs.lseek64 = dlsym(RTLD_NEXT, "lseek64");
	// g_xLibcFuncs.write = dlsym(RTLD_NEXT, "write");
	g_xLibcFuncs.close = dlsym(RTLD_NEXT, "close");
	g_xLibcFuncs.__close_nocancel = dlsym(RTLD_NEXT, "__close_nocancel");
	if (g_xLibcFuncs.__close_nocancel == NULL) {
		log_msg("__close_nocancel not found; falling back to close\n");
		g_xLibcFuncs.__close_nocancel = g_xLibcFuncs.close;
	}
	// g_xLibcFuncs.openat = dlsym(RTLD_NEXT, "openat");
	// g_xLibcFuncs.openat64 = dlsym(RTLD_NEXT, "openat64");
	// g_xLibcFuncs.fopen = dlsym(RTLD_NEXT, "fopen");
	// g_xLibcFuncs.fopen64 = dlsym(RTLD_NEXT, "fopen64");
	g_xLibcFuncs.opendir = dlsym(RTLD_NEXT, "opendir");
	// g_xLibcFuncs.opendir64 = dlsym(RTLD_NEXT, "opendir64");
	// g_xLibcFuncs.fdopendir = dlsym(RTLD_NEXT, "fdopendir");
	g_xLibcFuncs.readdir = dlsym(RTLD_NEXT, "readdir");
	// g_xLibcFuncs.readdir64 = dlsym(RTLD_NEXT, "readdir64");
	// g_xLibcFuncs.readdir_r = dlsym(RTLD_NEXT, "readdir_r");
	// g_xLibcFuncs.readdir64_r = dlsym(RTLD_NEXT, "readdir64_r");
	g_xLibcFuncs.closedir = dlsym(RTLD_NEXT, "closedir");
	// g_xLibcFuncs.scandir = dlsym(RTLD_NEXT, "scandir");
	g_xLibcFuncs.access = dlsym(RTLD_NEXT, "access");
	// g_xLibcFuncs.faccessat = dlsym(RTLD_NEXT, "faccessat");
	g_xLibcFuncs.__xstat = dlsym(RTLD_NEXT, "__xstat");
	// g_xLibcFuncs.__lxstat = dlsym(RTLD_NEXT, "__lxstat");
	// g_xLibcFuncs.__fxstat = dlsym(RTLD_NEXT, "__fxstat");
	// g_xLibcFuncs.__xstat64 = dlsym(RTLD_NEXT, "__xstat64");
	// g_xLibcFuncs.__lxstat64 = dlsym(RTLD_NEXT, "__lxstat64");
	// g_xLibcFuncs.__fxstat64 = dlsym(RTLD_NEXT, "__fxstat64");

	g_bLibcFuncsPopulated = true;
}

_Static_assert(sizeof(struct LibcFunctions) % sizeof(void *) == 0,
	       "LibcFunctions must contain pointer-sized fields only");

static bool bLibcFuncPtrsLoaded(const struct LibcFunctions *funcs)
{
	const unsigned char *bytes = (const unsigned char *)funcs;
	size_t count = sizeof(*funcs) / sizeof(void *);

	for (size_t i = 0; i < count; i++) {
		void *func = NULL;

		memcpy(&func, bytes + i * sizeof(func), sizeof(func));
		if (func == NULL)
			return false;
	}

	return true;
}

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
	g_xLibcFuncs = g_xTrampoline;
}

static void install_hooks(void)
{
	int rv;

	if (!bLibcFuncPtrsLoaded(&g_xLibcFuncs)) {
		log_msg("hook install failed: missing real function\n");
		return;
	}

	g_xTrampoline = g_xLibcFuncs;

	hooks_funchook = funchook_create();
	if (hooks_funchook == NULL) {
		log_msg("funchook_create failed\n");
		return;
	}

	if (prepare_hook("open", (void **)&g_xLibcFuncs.open, (void *)sqfs_open) != 0 ||
	    prepare_hook("read", (void **)&g_xLibcFuncs.read, (void *)sqfs_read) != 0 ||
	    prepare_hook("pread", (void **)&g_xLibcFuncs.pread, (void *)sqfs_pread) != 0 ||
	    prepare_hook("lseek", (void **)&g_xLibcFuncs.lseek, (void *)sqfs_lseek) != 0 ||
	    ((void *)g_xLibcFuncs.pread64 != (void *)g_xLibcFuncs.pread &&
	     prepare_hook("pread64", (void **)&g_xLibcFuncs.pread64,
			  (void *)sqfs_pread64) != 0) ||
	    ((void *)g_xLibcFuncs.lseek64 != (void *)g_xLibcFuncs.lseek &&
	     prepare_hook("lseek64", (void **)&g_xLibcFuncs.lseek64,
			  (void *)sqfs_lseek64) != 0) ||
	    prepare_hook("close", (void **)&g_xLibcFuncs.close, (void *)sqfs_close) != 0 ||
	    (g_xTrampoline.__close_nocancel != NULL &&
	     (void *)g_xTrampoline.__close_nocancel != (void *)g_xTrampoline.close &&
	     prepare_hook("__close_nocancel", (void **)&g_xLibcFuncs.__close_nocancel,
			  (void *)sqfs_close_nocancel) != 0) ||
	    prepare_hook("opendir", (void **)&g_xLibcFuncs.opendir, (void *)sqfs_opendir) != 0 ||
	    prepare_hook("readdir", (void **)&g_xLibcFuncs.readdir, (void *)sqfs_readdir) != 0 ||
	    prepare_hook("closedir", (void **)&g_xLibcFuncs.closedir, (void *)sqfs_closedir) != 0 ||
	    prepare_hook("access", (void **)&g_xLibcFuncs.access, (void *)sqfs_access) != 0 ||
	    prepare_hook("__xstat", (void **)&g_xLibcFuncs.__xstat, (void *)sqfs_xstat) != 0) {
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
	log_msg("  open=%p\n", (void *)g_xLibcFuncs.open);
	log_msg("  read=%p\n", (void *)g_xLibcFuncs.read);
	log_msg("  pread=%p\n", (void *)g_xLibcFuncs.pread);
	log_msg("  pread64=%p\n", (void *)g_xLibcFuncs.pread64);
	log_msg("  lseek=%p\n", (void *)g_xLibcFuncs.lseek);
	log_msg("  lseek64=%p\n", (void *)g_xLibcFuncs.lseek64);
	log_msg("  close=%p\n", (void *)g_xLibcFuncs.close);
	log_msg("  __close_nocancel=%p\n", (void *)g_xLibcFuncs.__close_nocancel);
	log_msg("  opendir=%p\n", (void *)g_xLibcFuncs.opendir);
	log_msg("  readdir=%p\n", (void *)g_xLibcFuncs.readdir);
	log_msg("  closedir=%p\n", (void *)g_xLibcFuncs.closedir);
	log_msg("  access=%p\n", (void *)g_xLibcFuncs.access);
	log_msg("  __xstat=%p\n", (void *)g_xLibcFuncs.__xstat);
}

/* ------------------------------------------------------------------ */
/* so constructor / destructor                                        */
/* ------------------------------------------------------------------ */

__attribute__((constructor))
static void hooksqfs_init(void)
{
	sqfs_mutex_init_once();

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
