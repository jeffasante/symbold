/* WebAssembly exports: the host copies bytes into the returned buffers. */
#include "symbold.h"

#include <stdio.h>
#include <stdlib.h>

#define EXPORT(name) __attribute__((export_name(name)))

static sd_program program;
static uint8_t image[SD_PIXELS];
static int32_t features[SD_N_FEATURES];
static uint32_t last_cost;
static char error[160];

EXPORT("alloc") uint8_t *wasm_alloc(uint32_t len) { return malloc(len); }
EXPORT("dealloc") void wasm_dealloc(uint8_t *p) { free(p); }
EXPORT("image") uint8_t *wasm_image(void) { return image; }
EXPORT("features") int32_t *wasm_features(void) { return features; }
EXPORT("error") const char *wasm_error(void) { return error; }
EXPORT("cost") uint32_t wasm_cost(void) { return last_cost; }
EXPORT("n_instructions") uint32_t wasm_n_instructions(void) { return program.n_instructions; }

/* Returns 0 on success, -1 with a message at error(). Replaces any loaded program. */
EXPORT("load") int wasm_load(const uint8_t *data, uint32_t len) {
    sd_program next;
    if (sd_program_parse(data, len, &next, error, sizeof error) != 0) return -1;
    if (next.feature_set_id != SD_FEATURE_SET_ID || next.n_features != SD_N_FEATURES) {
        snprintf(error, sizeof error, "program needs feature set %u with %u features",
                 next.feature_set_id, next.n_features);
        sd_program_free(&next);
        return -1;
    }
    sd_program_free(&program);
    program = next;
    return 0;
}

/* Classifies image(); returns 0-9, 255 for UNKNOWN, or -1 if no program is loaded. */
EXPORT("predict") int wasm_predict(void) {
    if (!program.ins) return -1;
    sd_features(image, features);
    return sd_run(&program, features, &last_cost);
}
