/* Loads, validates and executes exported programs (docs/program-format.md). */
#include "symbold.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t sd_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int fail(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

enum { USES_LABEL = 1, USES_FEATURE = 2, USES_THRESHOLD = 4, USES_TARGET = 8 };

static int operands_of(uint8_t op) {
    switch (op) {
    case SD_RETURN: return USES_LABEL;
    case SD_BR_GT: return USES_FEATURE | USES_THRESHOLD | USES_TARGET;
    case SD_LOAD_LE:
    case SD_AND_LE:
    case SD_OR_LE: return USES_FEATURE | USES_THRESHOLD;
    case SD_NOT: return 0;
    case SD_JUMP_IF:
    case SD_JUMP: return USES_TARGET;
    default: return -1;
    }
}

int sd_program_parse(const uint8_t *data, size_t len, sd_program *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    if (len < SD_HEADER_SIZE) return fail(err, errlen, "file shorter than header");
    if (memcmp(data, "SDPG", 4) != 0) return fail(err, errlen, "bad magic");
    if (rd16(data + 4) != SD_VERSION) return fail(err, errlen, "unsupported version %u", rd16(data + 4));
    if (rd16(data + 6) != SD_HEADER_SIZE || rd16(data + 20) != SD_N_LABELS || rd16(data + 22) != 0 ||
        rd32(data + 28) != 0)
        return fail(err, errlen, "bad header field");
    uint32_t fs = rd32(data + 8), nf = rd32(data + 12), n = rd32(data + 16), crc = rd32(data + 24);
    if (nf < 1 || nf > SD_MAX_FEATURES) return fail(err, errlen, "n_features %u out of range", nf);
    if (n < 1 || n > SD_MAX_INSTRUCTIONS) return fail(err, errlen, "instruction count %u out of range", n);
    if (len != SD_HEADER_SIZE + (size_t)SD_INSTRUCTION_SIZE * n)
        return fail(err, errlen, "file length does not match instruction count");
    const uint8_t *body = data + SD_HEADER_SIZE;
    if (sd_crc32(body, len - SD_HEADER_SIZE) != crc) return fail(err, errlen, "CRC mismatch");

    sd_instruction *ins = malloc(sizeof *ins * n);
    if (!ins) return fail(err, errlen, "out of memory");
    for (uint32_t pc = 0; pc < n; pc++) {
        const uint8_t *r = body + (size_t)SD_INSTRUCTION_SIZE * pc;
        sd_instruction *i = &ins[pc];
        i->op = r[0];
        i->label = r[1];
        i->feature = rd16(r + 2);
        i->threshold = (int32_t)rd32(r + 4);
        i->target = rd32(r + 8);
        int uses = operands_of(i->op);
        const char *why = NULL;
        if (uses < 0) why = "unknown opcode";
        else if (!(uses & USES_LABEL) && i->label) why = "label must be 0";
        else if (!(uses & USES_FEATURE) && i->feature) why = "feature must be 0";
        else if (!(uses & USES_THRESHOLD) && i->threshold) why = "threshold must be 0";
        else if (!(uses & USES_TARGET) && i->target) why = "target must be 0";
        else if ((uses & USES_FEATURE) && i->feature >= nf) why = "feature out of range";
        else if (i->op == SD_RETURN && i->label >= SD_N_LABELS && i->label != SD_UNKNOWN) why = "invalid label";
        else if ((uses & USES_TARGET) && !(i->target > pc && i->target < n)) why = "target is not a forward jump inside the program";
        if (why) {
            free(ins);
            return fail(err, errlen, "%u: %s", pc, why);
        }
    }
    if (ins[n - 1].op != SD_RETURN) {
        free(ins);
        return fail(err, errlen, "last instruction must be RETURN");
    }
    out->feature_set_id = fs;
    out->n_features = nf;
    out->n_instructions = n;
    out->ins = ins;
    return 0;
}

int sd_program_load(const char *path, sd_program *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, errlen, "cannot open %s", path);
    size_t max = SD_HEADER_SIZE + (size_t)SD_INSTRUCTION_SIZE * SD_MAX_INSTRUCTIONS;
    uint8_t *buf = malloc(max + 1);
    if (!buf) {
        fclose(f);
        return fail(err, errlen, "out of memory");
    }
    size_t len = fread(buf, 1, max + 1, f);
    int read_error = ferror(f);
    fclose(f);
    int rc = read_error ? fail(err, errlen, "read error on %s", path)
           : len > max  ? fail(err, errlen, "file too large")
                        : sd_program_parse(buf, len, out, err, errlen);
    free(buf);
    return rc;
}

void sd_program_free(sd_program *p) {
    free(p->ins);
    memset(p, 0, sizeof *p);
}

/* No bounds checks: sd_program_parse guarantees every feature index is in
   range, every jump is forward and the program ends in RETURN. */
uint8_t sd_run(const sd_program *p, const int32_t *x, uint32_t *cost) {
    const sd_instruction *ins = p->ins;
    uint32_t pc = 0, steps = 0;
    int flag = 0;
    for (;;) {
        const sd_instruction *i = &ins[pc];
        steps++;
        switch (i->op) {
        case SD_RETURN:
            if (cost) *cost = steps;
            return i->label;
        case SD_BR_GT:
            pc = x[i->feature] > i->threshold ? i->target : pc + 1;
            continue;
        case SD_LOAD_LE: flag = x[i->feature] <= i->threshold; break;
        case SD_AND_LE: flag = flag && x[i->feature] <= i->threshold; break;
        case SD_OR_LE: flag = flag || x[i->feature] <= i->threshold; break;
        case SD_NOT: flag = !flag; break;
        case SD_JUMP_IF:
            pc = flag ? i->target : pc + 1;
            continue;
        case SD_JUMP:
            pc = i->target;
            continue;
        }
        pc++;
    }
}
