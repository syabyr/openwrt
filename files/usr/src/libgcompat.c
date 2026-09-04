/*
 * glibc compat shim for running the glibc-built librknnrt.so on musl.
 * Provides the handful of glibc-internal symbols librknnrt references
 * that musl does not export (the *_finite math variants, mmap64,
 * __pthread_key_create, __strdup).
 *
 * librknnrt.so is patched with `patchelf --add-needed libgcompat.so`
 * so this shim is loaded automatically; no LD_PRELOAD needed.
 * Build (OpenWrt toolchain):
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
float   __powf_finite(float x, float y)    { return powf(x, y); }

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
