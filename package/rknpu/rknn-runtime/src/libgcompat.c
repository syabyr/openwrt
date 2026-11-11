/*
 * glibc compat shim for running Rockchip's glibc-built librknnrt.so and
 * rknn_server on musl.  Provides the glibc-internal symbols musl does not
 * export: the *_finite math aliases, mmap64, __pthread_key_create,
 * __strdup (used by librknnrt), and the backtrace API (used by
 * rknn_server for error logging).
 *
 * librknnrt.so is patched with `patchelf --add-needed libgcompat.so`, and
 * rknn_server is launched via an LD_PRELOAD wrapper, so this shim loads
 * automatically.
 *
 * Build (OpenWrt musl toolchain):
 *   aarch64-openwrt-linux-musl-gcc -O2 -shared -fPIC \
 *     -o libgcompat.so libgcompat.c -lm
 */
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>

float   __expf_finite(float x)             { return expf(x); }
double  __exp_finite(double x)             { return exp(x); }
float   __fmodf_finite(float x, float y)   { return fmodf(x, y); }
float   __log2f_finite(float x)            { return log2f(x); }
double  __log2_finite(double x)            { return log2(x); }
float   __logf_finite(float x)             { return logf(x); }
double  __log_finite(double x)             { return log(x); }
float   __powf_finite(float x, float y)    { return powf(x, y); }
double  __pow_finite(double x, double y)   { return pow(x, y); }

void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return mmap(addr, length, prot, flags, fd, offset);
}

int __pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
	return pthread_key_create(key, destructor);
}

char *__strdup(const char *s)
{
	return strdup(s);
}

/* glibc backtrace API: musl has no implementation.  rknn_server only
 * uses it to decorate error logs, so no-op stubs suffice. */
int backtrace(void **buffer, int size)
{
	(void)buffer;
	(void)size;
	return 0;
}

char **backtrace_symbols(void *const *buffer, int size)
{
	(void)buffer;
	(void)size;
	return NULL;
}

void backtrace_symbols_fd(void *const *buffer, int size, int fd)
{
	(void)buffer;
	(void)size;
	(void)fd;
}
