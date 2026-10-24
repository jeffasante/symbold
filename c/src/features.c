/* Feature set 1 (docs/program-format.md §4); every implementation must match bit for bit. */
#include "symbold.h"

#include <string.h>

enum {
    PX = 0, CELL = 784, ROW_INK = 833, COL_INK = 861, ROW_RUNS = 889, COL_RUNS = 917,
    INK = 945, COMPONENTS, HOLES, BBOX_HEIGHT, BBOX_WIDTH, CENTROID_ROW, CENTROID_COL,
    MIRROR_LR, MIRROR_TB, HOLE_AREA, HOLE_ROW
};

#define N SD_SIDE

/* Flood-fill the region of pixels equal to `want` containing (r0, c0), marking
   `seen`. Returns its size; sets *border if it touches the image edge and adds
   its row indices to *row_sum. */
static int fill(const uint8_t b[N][N], uint8_t seen[N][N], int r0, int c0, uint8_t want,
                int eight, int *border, long *row_sum) {
    static const int dr[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    static const int dc[8] = {0, 0, -1, 1, -1, 1, -1, 1};
    int stack[N * N];
    int top = 0, size = 0;
    stack[top++] = r0 * N + c0;
    seen[r0][c0] = 1;
    while (top) {
        int v = stack[--top], r = v / N, c = v % N;
        size++;
        *row_sum += r;
        if (r == 0 || c == 0 || r == N - 1 || c == N - 1) *border = 1;
        for (int k = 0; k < (eight ? 8 : 4); k++) {
            int rr = r + dr[k], cc = c + dc[k];
            if (rr < 0 || cc < 0 || rr >= N || cc >= N) continue;
            if (seen[rr][cc] || b[rr][cc] != want) continue;
            seen[rr][cc] = 1;
            stack[top++] = rr * N + cc;
        }
    }
    return size;
}

void sd_features(const uint8_t image[SD_PIXELS], int32_t out[SD_N_FEATURES]) {
    uint8_t b[N][N];
    memset(out, 0, sizeof(int32_t) * SD_N_FEATURES);
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) b[r][c] = image[r * N + c] >= 128;

    int ink = 0, rmin = N, rmax = -1, cmin = N, cmax = -1;
    long rsum = 0, csum = 0;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (!b[r][c]) continue;
            out[PX + r * N + c] = 1;
            out[CELL + (r / 4) * 7 + c / 4]++;
            out[ROW_INK + r]++;
            out[COL_INK + c]++;
            if (c == 0 || !b[r][c - 1]) out[ROW_RUNS + r]++;
            if (r == 0 || !b[r - 1][c]) out[COL_RUNS + c]++;
            if (b[r][N - 1 - c]) out[MIRROR_LR]++;
            if (b[N - 1 - r][c]) out[MIRROR_TB]++;
            ink++;
            rsum += r;
            csum += c;
            if (r < rmin) rmin = r;
            if (r > rmax) rmax = r;
            if (c < cmin) cmin = c;
            if (c > cmax) cmax = c;
        }
    }
    out[INK] = ink;
    if (ink) {
        out[BBOX_HEIGHT] = rmax - rmin + 1;
        out[BBOX_WIDTH] = cmax - cmin + 1;
        out[CENTROID_ROW] = (int32_t)(rsum / ink);
        out[CENTROID_COL] = (int32_t)(csum / ink);
    }

    uint8_t seen[N][N];
    memset(seen, 0, sizeof seen);
    int components = 0, holes = 0, hole_area = 0;
    long hole_rows = 0;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (seen[r][c]) continue;
            int border = 0;
            long row_sum = 0;
            if (b[r][c]) {
                fill(b, seen, r, c, 1, 1, &border, &row_sum);
                components++;
            } else {
                int size = fill(b, seen, r, c, 0, 0, &border, &row_sum);
                if (!border) {
                    holes++;
                    hole_area += size;
                    hole_rows += row_sum;
                }
            }
        }
    }
    out[COMPONENTS] = components;
    out[HOLES] = holes;
    out[HOLE_AREA] = hole_area;
    if (hole_area) out[HOLE_ROW] = (int32_t)(hole_rows / hole_area);
}
