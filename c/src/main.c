/* symbold command line runtime.

   symbold check    MODEL                  validate a program and print its shape
   symbold features IMAGES OUT             feature set 1 for raw 784-byte images -> int32 LE
   symbold run      MODEL FEATURES         run on raw int32 LE feature rows; prints "label cost"
   symbold predict  MODEL IMAGES           extract features and run; prints one label per line
   symbold bench    MODEL IMAGES LABELS    accuracy, latency and peak memory
*/
#include "symbold.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static void die(const char *msg) {
    fprintf(stderr, "symbold: %s\n", msg);
    exit(1);
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open input file");
    if (fseek(f, 0, SEEK_END) != 0) die("cannot seek input file");
    long size = ftell(f);
    if (size < 0) die("cannot size input file");
    rewind(f);
    uint8_t *buf = malloc(size ? (size_t)size : 1);
    if (!buf) die("out of memory");
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) die("short read");
    fclose(f);
    *len = (size_t)size;
    return buf;
}

static void load(const char *path, sd_program *p, int need_feature_set) {
    char err[256];
    if (sd_program_load(path, p, err, sizeof err) != 0) {
        fprintf(stderr, "symbold: %s: %s\n", path, err);
        exit(1);
    }
    if (need_feature_set && (p->feature_set_id != SD_FEATURE_SET_ID || p->n_features != SD_N_FEATURES)) {
        fprintf(stderr, "symbold: %s: needs feature set %u with %u features; this runtime has %u with %u\n",
                path, p->feature_set_id, p->n_features, SD_FEATURE_SET_ID, SD_N_FEATURES);
        exit(1);
    }
}

static const uint8_t *images_of(const char *path, size_t *n, uint8_t **owned) {
    size_t len;
    *owned = read_file(path, &len);
    if (len % SD_PIXELS) die("image file length is not a multiple of 784");
    *n = len / SD_PIXELS;
    return *owned;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmd_check(int argc, char **argv) {
    if (argc != 3) die("usage: symbold check MODEL");
    sd_program p;
    load(argv[2], &p, 0);
    printf("ok feature_set=%u n_features=%u instructions=%u\n", p.feature_set_id, p.n_features,
           p.n_instructions);
    sd_program_free(&p);
    return 0;
}

static int cmd_features(int argc, char **argv) {
    if (argc != 4) die("usage: symbold features IMAGES OUT");
    size_t n;
    uint8_t *owned;
    const uint8_t *img = images_of(argv[2], &n, &owned);
    FILE *f = fopen(argv[3], "wb");
    if (!f) die("cannot open output file");
    int32_t x[SD_N_FEATURES];
    uint8_t row[SD_N_FEATURES * 4];
    for (size_t k = 0; k < n; k++) {
        sd_features(img + k * SD_PIXELS, x);
        for (int j = 0; j < SD_N_FEATURES; j++) {
            uint32_t v = (uint32_t)x[j];
            row[4 * j] = (uint8_t)v;
            row[4 * j + 1] = (uint8_t)(v >> 8);
            row[4 * j + 2] = (uint8_t)(v >> 16);
            row[4 * j + 3] = (uint8_t)(v >> 24);
        }
        if (fwrite(row, 1, sizeof row, f) != sizeof row) die("write error");
    }
    if (fclose(f) != 0) die("write error");
    free(owned);
    return 0;
}

static int cmd_run(int argc, char **argv) {
    if (argc != 4) die("usage: symbold run MODEL FEATURES");
    sd_program p;
    load(argv[2], &p, 0);
    size_t len;
    uint8_t *raw = read_file(argv[3], &len);
    size_t row_bytes = (size_t)p.n_features * 4;
    if (len % row_bytes) die("feature file length is not a multiple of n_features * 4");
    int32_t *x = malloc(row_bytes);
    if (!x) die("out of memory");
    for (size_t k = 0; k < len / row_bytes; k++) {
        const uint8_t *r = raw + k * row_bytes;
        for (uint32_t j = 0; j < p.n_features; j++)
            x[j] = (int32_t)((uint32_t)r[4 * j] | ((uint32_t)r[4 * j + 1] << 8) |
                             ((uint32_t)r[4 * j + 2] << 16) | ((uint32_t)r[4 * j + 3] << 24));
        uint32_t cost;
        uint8_t label = sd_run(&p, x, &cost);
        printf("%u %u\n", label, cost);
    }
    free(x);
    free(raw);
    sd_program_free(&p);
    return 0;
}

static int cmd_predict(int argc, char **argv) {
    if (argc != 4) die("usage: symbold predict MODEL IMAGES");
    sd_program p;
    load(argv[2], &p, 1);
    size_t n;
    uint8_t *owned;
    const uint8_t *img = images_of(argv[3], &n, &owned);
    int32_t x[SD_N_FEATURES];
    for (size_t k = 0; k < n; k++) {
        sd_features(img + k * SD_PIXELS, x);
        printf("%u\n", sd_run(&p, x, NULL));
    }
    free(owned);
    sd_program_free(&p);
    return 0;
}

static int cmd_bench(int argc, char **argv) {
    if (argc != 5) die("usage: symbold bench MODEL IMAGES LABELS");
    sd_program p;
    load(argv[2], &p, 1);
    size_t n, nl;
    uint8_t *owned;
    const uint8_t *img = images_of(argv[3], &n, &owned);
    uint8_t *labels = read_file(argv[4], &nl);
    if (nl != n || n == 0) die("label count does not match image count");

    int32_t *feats = malloc(sizeof(int32_t) * SD_N_FEATURES * n);
    if (!feats) die("out of memory");
    double t0 = now();
    for (size_t k = 0; k < n; k++) sd_features(img + k * SD_PIXELS, feats + k * SD_N_FEATURES);
    double t1 = now();
    size_t correct = 0, unknown = 0;
    uint64_t cost_sum = 0;
    for (size_t k = 0; k < n; k++) {
        uint32_t cost;
        uint8_t y = sd_run(&p, feats + k * SD_N_FEATURES, &cost);
        correct += y == labels[k];
        unknown += y == SD_UNKNOWN;
        cost_sum += cost;
    }
    double t2 = now();
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    double rss_kib = (double)ru.ru_maxrss / 1024.0; /* bytes on macOS */
#else
    double rss_kib = (double)ru.ru_maxrss; /* KiB on Linux */
#endif
    double dn = (double)n;
    printf("{\"n\": %zu, \"accuracy\": %.6f, \"unknown_rate\": %.6f, \"mean_cost\": %.4f, "
           "\"features_us\": %.4f, \"interpret_us\": %.4f, \"program_bytes\": %zu, \"peak_rss_kib\": %.0f}\n",
           n, (double)correct / dn, (double)unknown / dn, (double)cost_sum / dn, (t1 - t0) / dn * 1e6,
           (t2 - t1) / dn * 1e6, (size_t)SD_HEADER_SIZE + (size_t)SD_INSTRUCTION_SIZE * p.n_instructions,
           rss_kib);
    free(feats);
    free(labels);
    free(owned);
    sd_program_free(&p);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) die("usage: symbold {check|features|run|predict|bench} ...");
    const char *c = argv[1];
    if (!strcmp(c, "check")) return cmd_check(argc, argv);
    if (!strcmp(c, "features")) return cmd_features(argc, argv);
    if (!strcmp(c, "run")) return cmd_run(argc, argv);
    if (!strcmp(c, "predict")) return cmd_predict(argc, argv);
    if (!strcmp(c, "bench")) return cmd_bench(argc, argv);
    die("unknown command");
    return 1;
}
