/*
 * rknn_bench.c - RKNPU performance benchmark for LubanCat-1N (RK3566)
 *
 * For each model given on the command line, measures:
 *   A. classic API full path : rknn_inputs_set + rknn_run per iteration
 *      (what a typical application loop costs, including input conversion)
 *   B. pure run              : input set once, only rknn_run in the loop
 *      (pure NPU execution, no per-iteration input work)
 *   C. zero-copy API         : rknn_create_mem + rknn_set_io_mem, then
 *      rknn_run in the loop (what a tuned application can achieve)
 *
 * Reports latency stats (avg/min/max/p50/p90/p99), throughput, CPU time
 * per iteration (NPU offload ratio) and peak NPU utilization sampled by
 * a child process through shared memory.
 *
 * Usage: rknn_bench <model.rknn> [model2.rknn ...] [iters]
 * Build: see build.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "rknn_api.h"

#define WARMUP   20

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void *read_file(const char *path, uint32_t *size_out)
{
	FILE *f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "open %s failed\n", path); return NULL; }
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *buf = malloc(sz);
	if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); return NULL; }
	fclose(f);
	*size_out = (uint32_t)sz;
	return buf;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

/* latency stats over an array of per-iteration times */
static void report_stats(const char *tag, double *t, int n)
{
	qsort(t, n, sizeof(double), cmp_double);
	double sum = 0;
	for (int i = 0; i < n; i++) sum += t[i];
	printf("  %-22s avg %7.3f ms  min %7.3f  max %7.3f  p50 %7.3f  p90 %7.3f  p99 %7.3f  | %8.1f FPS\n",
	       tag, sum / n, t[0], t[n - 1],
	       t[n / 2], t[(int)(n * 0.90)], t[(int)(n * 0.99) - (n == 1 ? 0 : 1)],
	       n * 1000.0 / sum);
}

static void read_proc(const char *what, char *buf, size_t len)
{
	snprintf(buf, len, "/proc/rknpu/%s", what);
	FILE *f = fopen(buf, "r");
	buf[0] = 0;
	if (f) { if (fgets(buf, len, f)) ; fclose(f); }
	else snprintf(buf, len, "<n/a>");
}

/* child: sample NPU load into shared int until killed; exit code = max % */
static void npu_load_sampler(int *shared)
{
	*shared = 0;
	for (;;) {
		char buf[128];
		read_proc("load", buf, sizeof(buf));
		int v = 0;
		if (sscanf(buf, " NPU load: %d%%", &v) != 1)
			sscanf(buf, "NPU load: %d%%", &v);
		if (v > *shared) *shared = v;
		usleep(100000);
	}
}

static pid_t start_sampler(int **shared)
{
	*shared = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
	               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	pid_t pid = fork();
	if (pid == 0) { npu_load_sampler(*shared); _exit(0); }
	return pid;
}

static void stop_sampler(pid_t pid, int *shared, const char *tag)
{
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	printf("  peak NPU load (%s): %d%%\n", tag, *shared);
	munmap(shared, sizeof(int));
}

static void cpu_usage(const struct rusage *r0, const struct rusage *r1,
                      int n, double wall_ms)
{
	double cpu = (r1->ru_utime.tv_sec - r0->ru_utime.tv_sec) * 1000.0
	           + (r1->ru_utime.tv_usec - r0->ru_utime.tv_usec) / 1000.0
	           + (r1->ru_stime.tv_sec - r0->ru_stime.tv_sec) * 1000.0
	           + (r1->ru_stime.tv_usec - r0->ru_stime.tv_usec) / 1000.0;
	printf("  CPU per iter: %.3f ms (%.0f%% of wall) - rest is NPU work\n",
	       cpu / n, 100.0 * cpu / wall_ms);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <model.rknn> [...more models] [iterations]\n", argv[0]);
		return 1;
	}
	int iters = 300;
	/* last arg is iteration count if numeric */
	char *end;
	long v = strtol(argv[argc - 1], &end, 10);
	if (*end == 0 && v > 0) { iters = (int)v; argc--; }

	char freq[64], volt[64];
	read_proc("freq", freq, sizeof(freq));
	read_proc("volt", volt, sizeof(volt));
	printf("=== RKNPU benchmark (NPU %sHz, %s uV) ===\n", freq, volt);

	for (int a = 1; a < argc; a++) {
		const char *model_path = argv[a];
		printf("\n--- model: %s (%d iterations) ---\n", model_path, iters);

		uint32_t model_size = 0;
		void *model_data = read_file(model_path, &model_size);
		if (!model_data) return 1;

		/* 1. init */
		rknn_context ctx = 0;
		double t0 = now_ms();
		if (rknn_init(&ctx, model_data, model_size, 0, NULL) < 0) {
			fprintf(stderr, "FAIL: rknn_init\n");
			return 1;
		}
		printf("  rknn_init: %.1f ms (%u bytes)\n", now_ms() - t0, model_size);

		rknn_sdk_version ver;
		if (rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver)) == 0)
			printf("  runtime %s / driver %s\n", ver.api_version, ver.drv_version);

		rknn_tensor_attr in_attr, out_attr;
		memset(&in_attr, 0, sizeof(in_attr));
		memset(&out_attr, 0, sizeof(out_attr));
		in_attr.index = 0;
		out_attr.index = 0;
		rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
		rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr, sizeof(out_attr));
		printf("  input[%u] %s type=%s fmt=%s n_elems=%u | output[%u] %s n_elems=%u\n",
		       in_attr.n_dims ? in_attr.dims[1] : 0, in_attr.name,
		       get_type_string(in_attr.type), get_format_string(in_attr.fmt),
		       in_attr.n_elems, out_attr.n_dims ? out_attr.dims[1] : 0,
		       out_attr.name, out_attr.n_elems);

		/* deterministic input (pattern, not semantic; benchmark only) */
		uint32_t n_in = in_attr.n_elems;
		uint32_t in_bytes = (in_attr.type == RKNN_TENSOR_FLOAT32)
		                    ? n_in * 4 : n_in;
		uint8_t *in_raw = malloc(in_bytes);
		for (uint32_t i = 0; i < in_bytes; i++)
			in_raw[i] = (uint8_t)(i * 37 + 11);

		double *t = malloc(iters * sizeof(double));
		struct rusage r0, r1;

		/* 2. warmup */
		for (int i = 0; i < WARMUP; i++) rknn_run(ctx, NULL);

		/* A. classic API: inputs_set + run per iteration */
		{
			rknn_input in;
			memset(&in, 0, sizeof(in));
			in.index = 0;
			in.buf = in_raw;
			in.size = in_bytes;
			in.fmt = RKNN_TENSOR_NHWC;
			in.pass_through = 0;
			in.type = (in_attr.type == RKNN_TENSOR_FLOAT32)
			          ? RKNN_TENSOR_FLOAT32 : RKNN_TENSOR_INT8;

			int *load;
			pid_t pid = start_sampler(&load);
			getrusage(RUSAGE_SELF, &r0);
			double w0 = now_ms();
			for (int i = 0; i < iters; i++) {
				double s = now_ms();
				rknn_inputs_set(ctx, 1, &in);
				rknn_run(ctx, NULL);
				t[i] = now_ms() - s;
				if ((i + 1) % 100 == 0) { printf("."); fflush(stdout); }
			}
			double wall = now_ms() - w0;
			getrusage(RUSAGE_SELF, &r1);
			stop_sampler(pid, load, "classic");
			report_stats("classic inputs_set+run", t, iters);
			cpu_usage(&r0, &r1, iters, wall);
		}

		/* B. pure run: input already set (from loop A's last iteration) */
		{
			rknn_input in;
			memset(&in, 0, sizeof(in));
			in.index = 0;
			in.buf = in_raw;
			in.size = in_bytes;
			in.fmt = RKNN_TENSOR_NHWC;
			in.pass_through = 0;
			in.type = (in_attr.type == RKNN_TENSOR_FLOAT32)
			          ? RKNN_TENSOR_FLOAT32 : RKNN_TENSOR_INT8;
			rknn_inputs_set(ctx, 1, &in);  /* refresh input state */

			int *load;
			pid_t pid = start_sampler(&load);
			getrusage(RUSAGE_SELF, &r0);
			double w0 = now_ms();
			for (int i = 0; i < iters; i++) {
				double s = now_ms();
				rknn_run(ctx, NULL);
				t[i] = now_ms() - s;
			}
			double wall = now_ms() - w0;
			getrusage(RUSAGE_SELF, &r1);
			stop_sampler(pid, load, "pure-run");
			report_stats("pure rknn_run", t, iters);
			cpu_usage(&r0, &r1, iters, wall);
		}

		/* C. zero-copy API (opt-in: this path has crashed the kernel via
		 * watchdog reset on driver v0.9.8 - only enable deliberately) */
		if (getenv("RKNN_BENCH_ZERO_COPY")) {
			rknn_tensor_mem *in_mem = rknn_create_mem(ctx, in_attr.size_with_stride);
			rknn_tensor_mem *out_mem = rknn_create_mem(ctx, out_attr.size);
			if (in_mem && out_mem &&
			    rknn_set_io_mem(ctx, in_mem, &in_attr) == 0 &&
			    rknn_set_io_mem(ctx, out_mem, &out_attr) == 0) {
				memset(in_mem->virt_addr, 0x55, in_attr.size_with_stride);

				int *load;
				pid_t pid = start_sampler(&load);
				getrusage(RUSAGE_SELF, &r0);
				double w0 = now_ms();
				for (int i = 0; i < iters; i++) {
					double s = now_ms();
					rknn_run(ctx, NULL);
					t[i] = now_ms() - s;
				}
				double wall = now_ms() - w0;
				getrusage(RUSAGE_SELF, &r1);
				stop_sampler(pid, load, "zero-copy");
				report_stats("zero-copy rknn_run", t, iters);
				cpu_usage(&r0, &r1, iters, wall);

				/* sanity: output must be written by the NPU */
				int nonzero = 0;
				uint8_t *o = (uint8_t *)out_mem->virt_addr;
				for (uint32_t i = 0; i < out_attr.size; i++)
					if (o[i]) { nonzero = 1; break; }
				printf("  zero-copy output buffer written by NPU: %s\n",
				       nonzero ? "yes" : "NO (suspicious)");
			} else {
				printf("  zero-copy API unavailable on this runtime, skipped\n");
			}
			if (in_mem) rknn_destroy_mem(ctx, in_mem);
			if (out_mem) rknn_destroy_mem(ctx, out_mem);
		}

		/* power domain should idle down after inference stops */
		char power[64];
		usleep(300000);
		read_proc("power", power, sizeof(power));
		printf("  NPU power domain after 300 ms idle: %s\n", power);

		rknn_destroy(ctx);
		free(t);
		free(in_raw);
		free(model_data);
	}
	printf("\nDONE\n");
	return 0;
}
