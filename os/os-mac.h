#ifndef FIO_OS_APPLE_H
#define FIO_OS_APPLE_H

#include <errno.h>
#include <sys/sysctl.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif

#define FIO_HAVE_POSIXAIO
#define FIO_USE_GENERIC_BDEV_SIZE
#define FIO_USE_GENERIC_RAND

#define OS_MAP_ANON		MAP_ANON

typedef unsigned long os_cpu_mask_t;
typedef unsigned int clockid_t;
typedef off_t off64_t;

static inline int blockdev_invalidate_cache(int fd)
{
	return EINVAL;
}

static inline unsigned long long os_phys_mem(void)
{
	int mib[2] = { CTL_HW, HW_PHYSMEM };
	unsigned long long mem;
	size_t len = sizeof(mem);

	sysctl(mib, 2, &mem, &len, NULL, 0);
	return mem;
}
#endif
