/*
 * Minimal RKNN application-layer smoke test for LubanCat 1N (RK3566).
 *
 * Loads resnet18_for_rk3566_rk3568.rknn, runs one inference with a
 * deterministic pseudo-random input and prints the top-5 outputs.
 * Exercises the full path: librknnrt -> DRM ioctl -> rknpu driver -> NPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "rknn_api.h"

static unsigned int seed = 42;
static float frand(void)
{
	/* deterministic xorshift-based float in [-1, 1] */
	seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
	return ((float)(seed % 20000) - 10000.0f) / 10000.0f;
}

static int cmp(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;
	return (x < y) - (x > y);
}

int main(int argc, char **argv)
{
	const char *model_path = argv[1] ? argv[1] : "resnet18.rknn";
	FILE *f = fopen(model_path, "rb");
	if (!f) { perror("open model"); return 1; }
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *model = malloc(sz);
	if (fread(model, 1, sz, f) != (size_t)sz) { perror("read model"); return 1; }
	fclose(f);
	printf("model %s: %ld bytes\n", model_path, sz);

	rknn_context ctx = 0;
	int ret = rknn_init(&ctx, model, sz, 0, NULL);
	if (ret < 0) { fprintf(stderr, "rknn_init failed: %d\n", ret); return 1; }
	printf("rknn_init ok, ctx=%llu\n", (unsigned long long)ctx);

	rknn_input_output_num io;
	ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io));
	if (ret < 0) { fprintf(stderr, "query io num failed: %d\n", ret); return 1; }
	printf("n_input=%u n_output=%u\n", io.n_input, io.n_output);

	rknn_tensor_attr attr;
	memset(&attr, 0, sizeof(attr));
	attr.index = 0;
	ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
	if (ret < 0) { fprintf(stderr, "query input attr failed: %d\n", ret); return 1; }
	printf("input[0]: %s [%d,%d,%d] fmt=%d type=%d n_elems=%llu\n",
	       attr.name, attr.dims[0], attr.dims[1], attr.dims[2],
	       attr.fmt, attr.type, (unsigned long long)attr.n_elems);

	rknn_tensor_attr out_attr;
	memset(&out_attr, 0, sizeof(out_attr));
	out_attr.index = 0;
	ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr, sizeof(out_attr));
	if (ret < 0) { fprintf(stderr, "query output attr failed: %d\n", ret); return 1; }
	printf("output[0]: %s type=%d n_elems=%llu\n",
	       out_attr.name, out_attr.type, (unsigned long long)out_attr.n_elems);

	/* deterministic pseudo-random float input */
	float *in = malloc(attr.n_elems * sizeof(float));
	for (uint64_t i = 0; i < attr.n_elems; i++)
		in[i] = frand();

	rknn_input inputs[1];
	memset(inputs, 0, sizeof(inputs));
	inputs[0].index = 0;
	inputs[0].type = RKNN_TENSOR_FLOAT32;
	inputs[0].fmt = RKNN_TENSOR_NHWC;
	inputs[0].size = attr.n_elems * sizeof(float);
	inputs[0].buf = in;
	ret = rknn_inputs_set(ctx, 1, inputs);
	if (ret < 0) { fprintf(stderr, "rknn_inputs_set failed: %d\n", ret); return 1; }

	/* NPU clock and load before the run */
	{
		FILE *pf = fopen("/proc/rknpu/load", "r");
		if (pf) { char buf[128]; size_t n = fread(buf, 1, 127, pf);
			if (n) { buf[n] = 0; printf("npu load before: %s", buf); } fclose(pf); }
	}

	printf("running inference...\n");
	int loops = argc > 2 ? atoi(argv[2]) : 1;
	for (int l = 0; l < loops; l++) {
	ret = rknn_run(ctx, NULL);
	if (ret < 0) { fprintf(stderr, "rknn_run failed: %d\n", ret); return 1; }

	rknn_output outputs[1];
	memset(outputs, 0, sizeof(outputs));
	outputs[0].want_float = 1;
	ret = rknn_outputs_get(ctx, 1, outputs, NULL);
	if (ret < 0) { fprintf(stderr, "rknn_outputs_get failed: %d\n", ret); return 1; }

	{
		FILE *pf = fopen("/proc/rknpu/load", "r");
		if (pf) { char buf[128]; size_t n = fread(buf, 1, 127, pf);
			if (n) { buf[n] = 0; printf("npu load after:  %s", buf); } fclose(pf); }
	}

	uint64_t n_out = out_attr.n_elems;
	float *out = (float *)outputs[0].buf;
	printf("output[0]: %llu floats\n", (unsigned long long)n_out);

	/* top-5 */
	size_t n = (size_t)n_out;
	float *tmp = malloc(n * sizeof(float));
	memcpy(tmp, out, n * sizeof(float));
	qsort(tmp, n, sizeof(float), cmp);
	for (int k = 0; k < 5; k++) {
		for (size_t i = 0; i < n; i++)
			if (out[i] == tmp[k]) {
				printf("top%d: class=%zu value=%f\n", k + 1, i, out[i]);
				break;
			}
	}
	free(tmp);

	/* simple checksum of all outputs for reproducibility */
	double sum = 0;
	for (size_t i = 0; i < n; i++) sum += out[i];
	printf("output sum (checksum) = %.6f\n", sum);

	rknn_outputs_release(ctx, 1, outputs);
	}
	rknn_destroy(ctx);
	free(in);
	free(model);
	printf("PASS: rknn inference completed on NPU\n");
	return 0;
}
