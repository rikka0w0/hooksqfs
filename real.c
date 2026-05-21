#define _GNU_SOURCE

#include "real.h"

#include <stdbool.h>
#include <dlfcn.h>

static bool populated = false;
struct LibcFunctions g_LibcFuncs = {0};

struct LibcFunctions g_xTrampoline = {0};

void vPopulateLibcFuncPtrs(void) {
	if (populated)
		return;

	g_LibcFuncs.open = dlsym(RTLD_NEXT, "open");
	g_LibcFuncs.open64 = dlsym(RTLD_NEXT, "open64");
	g_LibcFuncs.write = dlsym(RTLD_NEXT, "write");
	g_LibcFuncs.close = dlsym(RTLD_NEXT, "close");
	g_LibcFuncs.openat = dlsym(RTLD_NEXT, "openat");
	g_LibcFuncs.openat64 = dlsym(RTLD_NEXT, "openat64");
	g_LibcFuncs.fopen = dlsym(RTLD_NEXT, "fopen");
	g_LibcFuncs.fopen64 = dlsym(RTLD_NEXT, "fopen64");
	g_LibcFuncs.opendir = dlsym(RTLD_NEXT, "opendir");
	g_LibcFuncs.opendir64 = dlsym(RTLD_NEXT, "opendir64");
	g_LibcFuncs.fdopendir = dlsym(RTLD_NEXT, "fdopendir");
	g_LibcFuncs.readdir = dlsym(RTLD_NEXT, "readdir");
	g_LibcFuncs.readdir64 = dlsym(RTLD_NEXT, "readdir64");
	g_LibcFuncs.readdir_r = dlsym(RTLD_NEXT, "readdir_r");
	g_LibcFuncs.readdir64_r = dlsym(RTLD_NEXT, "readdir64_r");
	g_LibcFuncs.closedir = dlsym(RTLD_NEXT, "closedir");
	g_LibcFuncs.scandir = dlsym(RTLD_NEXT, "scandir");
	g_LibcFuncs.access = dlsym(RTLD_NEXT, "access");
	g_LibcFuncs.faccessat = dlsym(RTLD_NEXT, "faccessat");
	g_LibcFuncs.__xstat = dlsym(RTLD_NEXT, "__xstat");
	g_LibcFuncs.__lxstat = dlsym(RTLD_NEXT, "__lxstat");
	g_LibcFuncs.__fxstat = dlsym(RTLD_NEXT, "__fxstat");
#ifdef __USE_LARGEFILE64
	g_LibcFuncs.__xstat64 = dlsym(RTLD_NEXT, "__xstat64");
	g_LibcFuncs.__lxstat64 = dlsym(RTLD_NEXT, "__lxstat64");
	g_LibcFuncs.__fxstat64 = dlsym(RTLD_NEXT, "__fxstat64");
#endif

	populated = true;
}
