/*
 * rknn_test.c - application-level RKNPU verification for LubanCat-1N (RK3566)
 *
 * Loads mobilenet_v1.rknn via librknnrt, runs inference on dog_224x224.jpg,
 * prints SDK/driver versions, top-5 classification and NPU utilization.
 *
 * Build (OpenWrt musl toolchain):
 *   aarch64-openwrt-linux-musl-gcc -O2 -o rknn_test rknn_test.c \
 *       -I<incdir> -L<libdir> -Wl,--allow-shlib-undefined -l:librknnrt.so -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <sys/wait.h>

#include "rknn_api.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define LABEL_MAX 1100
#define LABEL_LEN 128
static char labels[LABEL_MAX][LABEL_LEN];
static int nlabels = 0;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void read_npu_load(const char *tag)
{
    FILE *f = fopen("/sys/kernel/debug/rknpu/load", "r");
    if (!f) { printf("[npu-load] %s: <unavailable>\n", tag); return; }
    char line[256];
    printf("[npu-load] %s:\n", tag);
    while (fgets(line, sizeof(line), f))
        printf("    %s", line);
    fclose(f);
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

static int load_labels(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (nlabels < LABEL_MAX &&
           fgets(labels[nlabels], LABEL_LEN, f)) {
        char *p = labels[nlabels];
        size_t len = strlen(p);
        while (len && (p[len-1] == '\n' || p[len-1] == '\r')) p[--len] = 0;
        /* strip "n02099601 " synset prefix if present */
        char *sp = strchr(p, ' ');
        if (sp && p[0] == 'n') memmove(p, sp + 1, len - (sp - p) + 1);
        nlabels++;
    }
    fclose(f);
    return 0;
}

static uint32_t g_n_out = 0; /* set after querying output attr */

static const char *label_for(int idx)
{
    if (nlabels == 0) return "<no-labels>";
    if (g_n_out == (uint32_t)nlabels) {
        /* 1000-class model: direct mapping */
        if (idx < nlabels) return labels[idx];
    } else {
        /* 1001-class model: 0 = background, i -> labels[i-1] */
        if (idx == 0) return "background";
        if (idx - 1 < nlabels) return labels[idx - 1];
    }
    return "<out-of-range>";
}

int main(int argc, char **argv)
{
    const char *model_path = argc > 1 ? argv[1] : "mobilenet_v1.rknn";
    const char *image_path = argc > 2 ? argv[2] : "dog_224x224.jpg";
    const char *label_path = argc > 3 ? argv[3] : "synset.txt";
    int ret;

    printf("=== RKNPU application-level test ===\n");

    /* 1. load model */
    uint32_t model_size = 0;
    void *model_data = read_file(model_path, &model_size);
    if (!model_data) { fprintf(stderr, "FAIL: read model %s\n", model_path); return 1; }
    printf("model: %s (%u bytes)\n", model_path, model_size);

    /* 2. init context */
    rknn_context ctx = 0;
    double t0 = now_ms();
    ret = rknn_init(&ctx, model_data, model_size, 0, NULL);
    double t1 = now_ms();
    if (ret < 0) {
        fprintf(stderr, "FAIL: rknn_init ret=%d (%.1f ms)\n", ret, t1 - t0);
        return 1;
    }
    printf("rknn_init: OK (%.1f ms)\n", t1 - t0);

    /* 3. query SDK/driver version */
    rknn_sdk_version ver;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver));
    if (ret < 0) { fprintf(stderr, "FAIL: query version ret=%d\n", ret); return 1; }
    printf("api_version: %s\ndrv_version: %s\n", ver.api_version, ver.drv_version);

    /* 4. query IO */
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { fprintf(stderr, "FAIL: query io num ret=%d\n", ret); return 1; }
    printf("n_input=%u n_output=%u\n", io_num.n_input, io_num.n_output);

    rknn_tensor_attr in_attr;
    memset(&in_attr, 0, sizeof(in_attr));
    in_attr.index = 0;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
    if (ret < 0) { fprintf(stderr, "FAIL: query input attr ret=%d\n", ret); return 1; }
    printf("input[0]: name=%s dims=[", in_attr.name);
    for (uint32_t i = 0; i < in_attr.n_dims; i++)
        printf("%s%u", i ? "," : "", in_attr.dims[i]);
    printf("] type=%s fmt=%s n_elems=%u size=%u\n",
           get_type_string(in_attr.type), get_format_string(in_attr.fmt),
           in_attr.n_elems, in_attr.size);

    rknn_tensor_attr out_attr;
    memset(&out_attr, 0, sizeof(out_attr));
    out_attr.index = 0;
    ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr, sizeof(out_attr));
    if (ret < 0) { fprintf(stderr, "FAIL: query output attr ret=%d\n", ret); return 1; }
    printf("output[0]: name=%s n_elems=%u type=%s qnt=%s zp=%d scale=%f\n",
           out_attr.name, out_attr.n_elems,
           get_type_string(out_attr.type),
           get_qnt_type_string(out_attr.qnt_type), out_attr.zp, out_attr.scale);

    /* 5. load image */
    int w, h, c;
    unsigned char *img = stbi_load(image_path, &w, &h, &c, 3);
    if (!img) { fprintf(stderr, "FAIL: load image %s\n", image_path); return 1; }
    printf("image: %s %dx%dx%d\n", image_path, w, h, c);
    if (in_attr.fmt != RKNN_TENSOR_NHWC) {
        fprintf(stderr, "FAIL: unexpected input fmt (need NHWC handling)\n");
        return 1;
    }
    if ((uint32_t)(w * h * 3) != in_attr.n_elems) {
        fprintf(stderr, "FAIL: image elems %d != input elems %u\n", w*h*3, in_attr.n_elems);
        return 1;
    }

    void *input_buf;
    uint32_t input_size;
    if (in_attr.type == RKNN_TENSOR_UINT8) {
        input_buf = img;
        input_size = in_attr.n_elems;
    } else if (in_attr.type == RKNN_TENSOR_INT8) {
        /* asymmetric int8: uint8 pixel - 128 */
        int8_t *qimg = malloc(in_attr.n_elems);
        for (uint32_t i = 0; i < in_attr.n_elems; i++)
            qimg[i] = (int8_t)((int)img[i] - 128);
        input_buf = qimg;
        input_size = in_attr.n_elems;
    } else if (in_attr.type == RKNN_TENSOR_FLOAT32) {
        float *fimg = malloc(in_attr.n_elems * sizeof(float));
        for (uint32_t i = 0; i < in_attr.n_elems; i++)
            fimg[i] = (float)img[i];
        input_buf = fimg;
        input_size = in_attr.n_elems * sizeof(float);
    } else {
        fprintf(stderr, "FAIL: unsupported input type\n");
        return 1;
    }

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.buf = input_buf;
    input.size = input_size;
    input.pass_through = 0;
    input.type = in_attr.type;
    input.fmt = RKNN_TENSOR_NHWC;
    ret = rknn_inputs_set(ctx, 1, &input);
    if (ret < 0) { fprintf(stderr, "FAIL: rknn_inputs_set ret=%d\n", ret); return 1; }

    /* 6. warmup + timed runs, with a child sampling NPU load */
    read_npu_load("before inference");
    rknn_run(ctx, NULL); /* warmup */

    pid_t pid = fork();
    if (pid == 0) {
        /* child: sample NPU load while parent runs inference */
        for (int i = 0; i < 5; i++) {
            usleep(150000);
            read_npu_load("during inference");
            fflush(stdout);
        }
        _exit(0);
    }

    const int N = 300;
    t0 = now_ms();
    for (int i = 0; i < N; i++) {
        ret = rknn_run(ctx, NULL);
        if (ret < 0) { fprintf(stderr, "\nFAIL: rknn_run #%d ret=%d\n", i, ret); return 1; }
    }
    t1 = now_ms();
    waitpid(pid, NULL, 0);
    printf("rknn_run: %d iterations in %.1f ms (%.2f ms/iter, ~%.1f FPS)\n",
           N, t1 - t0, (t1 - t0) / N, N * 1000.0 / (t1 - t0));

    /* 7. get output and top-5 */
    rknn_output output;
    memset(&output, 0, sizeof(output));
    output.want_float = 1;
    output.is_prealloc = 0;
    output.index = 0;
    ret = rknn_outputs_get(ctx, 1, &output, NULL);
    if (ret < 0) { fprintf(stderr, "FAIL: rknn_outputs_get ret=%d\n", ret); return 1; }

    float *out = (float *)output.buf;
    uint32_t n = out_attr.n_elems;
    printf("output[0]: %u classes (float)\n", n);
    g_n_out = n;

    int top_idx[5];
    float top_val[5];
    for (int k = 0; k < 5; k++) { top_idx[k] = -1; top_val[k] = -1e30f; }
    for (uint32_t i = 0; i < n; i++) {
        for (int k = 0; k < 5; k++) {
            if (out[i] > top_val[k]) {
                for (int j = 4; j > k; j--) { top_val[j] = top_val[j-1]; top_idx[j] = top_idx[j-1]; }
                top_val[k] = out[i]; top_idx[k] = (int)i;
                break;
            }
        }
    }
    /* softmax over top-5 for readable confidence */
    float sum = 0;
    for (int k = 0; k < 5; k++) sum += expf(top_val[k]);
    load_labels(label_path);
    printf("--- top-5 ---\n");
    for (int k = 0; k < 5; k++)
        printf("  %d: %-30s score=%.4f (p~%.1f%%)\n",
               top_idx[k], label_for(top_idx[k]), top_val[k], 100.0f * expf(top_val[k]) / sum);

    rknn_outputs_release(ctx, 1, &output);
    rknn_destroy(ctx);
    free(model_data);
    printf("=== PASS: RKNPU userspace inference works ===\n");
    return 0;
}
