#include "real.h"

#include <stdbool.h>
#include <dlfcn.h>

static bool populated = false;
struct RealFunctions real = {0};

void real_populate(void) {
	if (populated)
		return;

	real.open = dlsym(RTLD_NEXT, "open");
	real.open64 = dlsym(RTLD_NEXT, "open64");
	real.openat = dlsym(RTLD_NEXT, "openat");
	real.openat64 = dlsym(RTLD_NEXT, "openat64");
	real.fopen = dlsym(RTLD_NEXT, "fopen");
	real.fopen64 = dlsym(RTLD_NEXT, "fopen64");
	real.opendir = dlsym(RTLD_NEXT, "opendir");
	real.opendir64 = dlsym(RTLD_NEXT, "opendir64");
	real.fdopendir = dlsym(RTLD_NEXT, "fdopendir");
	real.readdir = dlsym(RTLD_NEXT, "readdir");
	real.readdir64 = dlsym(RTLD_NEXT, "readdir64");
	real.readdir_r = dlsym(RTLD_NEXT, "readdir_r");
	real.readdir64_r = dlsym(RTLD_NEXT, "readdir64_r");
	real.closedir = dlsym(RTLD_NEXT, "closedir");
	real.access = dlsym(RTLD_NEXT, "access");
	real.faccessat = dlsym(RTLD_NEXT, "faccessat");
	real.__xstat = dlsym(RTLD_NEXT, "__xstat");
	real.__lxstat = dlsym(RTLD_NEXT, "__lxstat");
	real.__fxstat = dlsym(RTLD_NEXT, "__fxstat");
#ifdef __USE_LARGEFILE64
	real.__xstat64 = dlsym(RTLD_NEXT, "__xstat64");
	real.__lxstat64 = dlsym(RTLD_NEXT, "__lxstat64");
	real.__fxstat64 = dlsym(RTLD_NEXT, "__fxstat64");
#endif

	populated = true;
}
