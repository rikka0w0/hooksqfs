#define _GNU_SOURCE

#include <stdio.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>

#include "funchook.h"

typedef DIR *(*opendir_func)(const char *name);

static funchook_t *funchook;
static opendir_func real_opendir;

static __thread int in_my_opendir;

DIR *my_opendir(const char *name)
{
	DIR *ret;
	int saved_errno;

	if (real_opendir == NULL) {
		errno = ENOSYS;
		return NULL;
	}

	if (in_my_opendir) {
		return real_opendir(name);
	}

	in_my_opendir = 1;

	fprintf(stderr, "[hook] my_opendir(\"%s\") called\n", name);

	ret = real_opendir(name);
	saved_errno = errno;

	fprintf(stderr, "[hook] opendir(\"%s\") = %p\n", name, (void *)ret);

	errno = saved_errno;
	in_my_opendir = 0;

	return ret;
}

static int install_opendir_hook(void)
{
	void *symbol;
	int rv;

	symbol = dlsym(RTLD_NEXT, "opendir");
	if (symbol == NULL) {
		fprintf(stderr, "[hook] dlsym(RTLD_NEXT, \"opendir\") failed: %s\n", dlerror());
		return -1;
	}

	/*
	 * Important:
	 *
	 * real_opendir initially points to libc opendir().
	 * After funchook_prepare(), funchook rewrites real_opendir
	 * to point to the trampoline.
	 */
	real_opendir = (opendir_func)symbol;

	funchook = funchook_create();
	if (funchook == NULL) {
		fprintf(stderr, "[hook] funchook_create() failed\n");
		return -1;
	}

	rv = funchook_prepare(
		funchook,
		(void **)&real_opendir,
		(void *)my_opendir
	);
	if (rv != 0) {
		fprintf(stderr,
			"[hook] funchook_prepare(opendir) failed: %s\n",
			funchook_error_message(funchook));
		funchook_destroy(funchook);
		funchook = NULL;
		real_opendir = NULL;
		return -1;
	}

	rv = funchook_install(funchook, 0);
	if (rv != 0) {
		fprintf(stderr,
			"[hook] funchook_install(opendir) failed: %s\n",
			funchook_error_message(funchook));
		funchook_destroy(funchook);
		funchook = NULL;
		real_opendir = NULL;
		return -1;
	}

	fprintf(stderr,
		"[hook] opendir hook installed: libc_opendir=%p trampoline=%p hook=%p\n",
		symbol,
		(void *)real_opendir,
		(void *)my_opendir);

	return 0;
}

static void uninstall_opendir_hook(void)
{
	int rv;

	if (funchook == NULL) {
		return;
	}

	rv = funchook_uninstall(funchook, 0);
	if (rv != 0) {
		fprintf(stderr,
			"[hook] funchook_uninstall(opendir) failed: %s\n",
			funchook_error_message(funchook));
	}

	rv = funchook_destroy(funchook);
	if (rv != 0) {
		fprintf(stderr, "[hook] funchook_destroy(opendir) failed\n");
	}

	funchook = NULL;
	real_opendir = NULL;

	fprintf(stderr, "[hook] opendir hook removed\n");
}

int main(void)
{
	DIR *d;

	if (install_opendir_hook() != 0) {
		return 1;
	}

	d = opendir("/tmp");
	fprintf(stderr, "[test] opendir returned %p\n", (void *)d);

	if (d != NULL) {
		closedir(d);
	}

	uninstall_opendir_hook();

	return 0;
}
