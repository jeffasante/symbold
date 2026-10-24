/* symbold C runtime. Format: docs/program-format.md (version 1). */
#ifndef SYMBOLD_H
#define SYMBOLD_H

#include <stddef.h>
#include <stdint.h>

#define SD_VERSION 1
#define SD_HEADER_SIZE 32
#define SD_INSTRUCTION_SIZE 12
#define SD_MAX_INSTRUCTIONS (1u << 20)
#define SD_MAX_FEATURES (1u << 16)
#define SD_N_LABELS 10
#define SD_UNKNOWN 255

#define SD_FEATURE_SET_ID 1
#define SD_N_FEATURES 956
#define SD_SIDE 28
#define SD_PIXELS (SD_SIDE * SD_SIDE)

enum sd_op {
    SD_RETURN = 1,
    SD_BR_GT = 2,
    SD_LOAD_LE = 3,
    SD_AND_LE = 4,
    SD_OR_LE = 5,
    SD_NOT = 6,
    SD_JUMP_IF = 7,
    SD_JUMP = 8
};

typedef struct {
    uint8_t op;
    uint8_t label;
    uint16_t feature;
    int32_t threshold;
    uint32_t target;
} sd_instruction;

typedef struct {
    uint32_t feature_set_id;
    uint32_t n_features;
    uint32_t n_instructions;
    sd_instruction *ins;
} sd_program;

/* Parse and fully validate a program. Returns 0 on success; on failure returns
   -1, writes a message to err, and leaves *out empty. */
int sd_program_parse(const uint8_t *data, size_t len, sd_program *out, char *err, size_t errlen);
int sd_program_load(const char *path, sd_program *out, char *err, size_t errlen);
void sd_program_free(sd_program *p);

/* Execute a validated program on a feature vector of length p->n_features.
   Writes the number of executed instructions to *cost if cost is non-NULL. */
uint8_t sd_run(const sd_program *p, const int32_t *x, uint32_t *cost);

/* Feature set 1 (docs/program-format.md §4). */
void sd_features(const uint8_t image[SD_PIXELS], int32_t out[SD_N_FEATURES]);

uint32_t sd_crc32(const uint8_t *data, size_t len);

#endif
