/*
 * Copyright (c) 2013 Clément Bœsch
 * Copyright (c) 2018 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "lut3d.h"

#include <float.h>

#include "libavutil/avstring.h"
#include "libavutil/file_open.h"

#define EXPONENT_MASK 0x7F800000
#define MANTISSA_MASK 0x007FFFFF
#define SIGN_MASK     0x80000000

static inline float sanitizef(float f)
{
    union av_intfloat32 t;
    t.f = f;

    if ((t.i & EXPONENT_MASK) == EXPONENT_MASK) {
        if ((t.i & MANTISSA_MASK) != 0) {
            // NAN
            return 0.0f;
        } else if (t.i & SIGN_MASK) {
            // -INF
            return -FLT_MAX;
        } else {
            // +INF
            return FLT_MAX;
        }
    }
    return f;
}

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static inline struct rgbvec lerp(const struct rgbvec *v0, const struct rgbvec *v1, float f)
{
    struct rgbvec v = {
        lerpf(v0->r, v1->r, f), lerpf(v0->g, v1->g, f), lerpf(v0->b, v1->b, f)
    };
    return v;
}

int ff_allocate_3dlut(AVFilterContext *ctx, LUT3DContext *lut3d, int lutsize, int prelut)
{
    int i;
    if (lutsize < 2 || lutsize > MAX_LEVEL) {
        av_log(ctx, AV_LOG_ERROR, "Too large or invalid 3D LUT size\n");
        return AVERROR(EINVAL);
    }

    av_freep(&lut3d->lut);
    lut3d->lut = av_malloc_array(lutsize * lutsize * lutsize, sizeof(*lut3d->lut));
    if (!lut3d->lut)
        return AVERROR(ENOMEM);

    if (prelut) {
        lut3d->prelut.size = PRELUT_SIZE;
        for (i = 0; i < 3; i++) {
            av_freep(&lut3d->prelut.lut[i]);
            lut3d->prelut.lut[i] = av_malloc_array(PRELUT_SIZE, sizeof(*lut3d->prelut.lut[0]));
            if (!lut3d->prelut.lut[i])
                return AVERROR(ENOMEM);
        }
    } else {
        lut3d->prelut.size = 0;
        for (i = 0; i < 3; i++) {
            av_freep(&lut3d->prelut.lut[i]);
        }
    }
    lut3d->lutsize = lutsize;
    lut3d->lutsize2 = lutsize * lutsize;
    return 0;
}

static int set_identity_matrix(AVFilterContext *ctx, LUT3DContext *lut3d, int size)
{
    int ret, i, j, k;
    const int size2 = size * size;
    const float c = 1. / (size - 1);

    ret = ff_allocate_3dlut(ctx, lut3d, size, 0);
    if (ret < 0)
        return ret;

    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];
                vec->r = k * c;
                vec->g = j * c;
                vec->b = i * c;
            }
        }
    }

    return 0;
}

#define MAX_LINE_SIZE 512

typedef struct LUTReader {
    const char *buf;
    size_t buf_len;
    size_t buf_pos;
} LUTReader;

static void lut_reader_init(LUTReader *r, const char* buf, size_t buf_len)
{
    r->buf = buf;
    r->buf_len = buf_len;
    r->buf_pos = 0;
}

static inline int lut_reader_eof(LUTReader *r)
{
    return r->buf_pos >= r->buf_len;
}

static int lut_reader_getc(LUTReader *r)
{
    if (lut_reader_eof(r))
        return EOF;
    return r->buf[r->buf_pos++];
}

static int lut_reader_next_word(LUTReader *r)
{

}

// Return start of line, advance position to start of next line.
static char *lut_reader_read_line(LUTReader *r, char *buf, size_t *size)
{
    size_t cur = 0;
    if (!size)
        return 0;
    buf[0] = '\0';
    while (cur + 1 < size) {
        unsigned char c = lut_reader_read_char(tr);
        if (!c)
            return lut_reader_eof(tr) ? cur : AVERROR_INVALIDDATA;
        if (c == '\r' || c == '\n')
            break;
        buf[cur++] = c;
        buf[cur] = '\0';
    }
    while (ff_text_peek_r8(tr) == '\r')
        ff_text_r8(tr);
    if (ff_text_peek_r8(tr) == '\n')
        ff_text_r8(tr);
    return cur;
}

static int lut_reader_skip_line(LUTReader *r)
{

}

static int skip_line(const char *p)
{
    while (*p && av_isspace(*p))
        p++;
    return !*p || *p == '#';
}

static char* fget_next_word(char* dst, int max, FILE* f)
{
    int c;
    char *p = dst;

    /* for null */
    max--;
    /* skip until next non whitespace char */
    while ((c = fgetc(f)) != EOF) {
        if (av_isspace(c))
            continue;

        *p++ = c;
        max--;
        break;
    }

    /* get max bytes or up until next whitespace char */
    for (; max > 0; max--) {
        if ((c = fgetc(f)) == EOF)
            break;

        if (av_isspace(c))
            break;

        *p++ = c;
    }

    *p = 0;
    if (p == dst)
        return NULL;
    return p;
}


#define NEXT_LINE(loop_cond) do {                           \
    if (!fgets(line, sizeof(line), f)) {                    \
        av_log(ctx, AV_LOG_ERROR, "Unexpected EOF\n");      \
        return AVERROR_INVALIDDATA;                         \
    }                                                       \
} while (loop_cond)

#define NEXT_LINE_OR_GOTO(loop_cond, label) do {            \
    if (!fgets(line, sizeof(line), f)) {                    \
        av_log(ctx, AV_LOG_ERROR, "Unexpected EOF\n");      \
        ret = AVERROR_INVALIDDATA;                          \
        goto label;                                         \
    }                                                       \
} while (loop_cond)

/* Basically r g and b float values on each line, with a facultative 3DLUTSIZE
 * directive; seems to be generated by Davinci */
static int parse_dat(AVFilterContext *ctx, LUT3DContext *lut3d, FILE *f)
{
    char line[MAX_LINE_SIZE];
    int ret, i, j, k, size, size2;

    lut3d->lutsize = size = 33;
    size2 = size * size;

    NEXT_LINE(skip_line(line));
    if (!strncmp(line, "3DLUTSIZE ", 10)) {
        size = strtol(line + 10, NULL, 0);

        NEXT_LINE(skip_line(line));
    }

    ret = ff_allocate_3dlut(ctx, lut3d, size, 0);
    if (ret < 0)
        return ret;

    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];
                if (k != 0 || j != 0 || i != 0)
                    NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%f %f %f", &vec->r, &vec->g, &vec->b) != 3)
                    return AVERROR_INVALIDDATA;
            }
        }
    }
    return 0;
}

/* Iridas format */
static int parse_cube(AVFilterContext *ctx, LUT3DContext *lut3d, FILE *f)
{
    char line[MAX_LINE_SIZE];
    float min[3] = {0.0, 0.0, 0.0};
    float max[3] = {1.0, 1.0, 1.0};

    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "LUT_3D_SIZE", 11)) {
            int ret, i, j, k;
            const int size = strtol(line + 12, NULL, 0);
            const int size2 = size * size;

            ret = ff_allocate_3dlut(ctx, lut3d, size, 0);
            if (ret < 0)
                return ret;

            for (k = 0; k < size; k++) {
                for (j = 0; j < size; j++) {
                    for (i = 0; i < size; i++) {
                        struct rgbvec *vec = &lut3d->lut[i * size2 + j * size + k];

                        do {
try_again:
                            NEXT_LINE(0);
                            if (!strncmp(line, "DOMAIN_", 7)) {
                                float *vals = NULL;
                                if      (!strncmp(line + 7, "MIN ", 4)) vals = min;
                                else if (!strncmp(line + 7, "MAX ", 4)) vals = max;
                                if (!vals)
                                    return AVERROR_INVALIDDATA;
                                av_sscanf(line + 11, "%f %f %f", vals, vals + 1, vals + 2);
                                av_log(ctx, AV_LOG_DEBUG, "min: %f %f %f | max: %f %f %f\n",
                                       min[0], min[1], min[2], max[0], max[1], max[2]);
                                goto try_again;
                            } else if (!strncmp(line, "TITLE", 5)) {
                                goto try_again;
                            }
                        } while (skip_line(line));
                        if (av_sscanf(line, "%f %f %f", &vec->r, &vec->g, &vec->b) != 3)
                            return AVERROR_INVALIDDATA;
                    }
                }
            }
            break;
        }
    }

    lut3d->scale.r = av_clipf(1. / (max[0] - min[0]), 0.f, 1.f);
    lut3d->scale.g = av_clipf(1. / (max[1] - min[1]), 0.f, 1.f);
    lut3d->scale.b = av_clipf(1. / (max[2] - min[2]), 0.f, 1.f);

    return 0;
}

/* Assume 17x17x17 LUT with a 16-bit depth
 * FIXME: it seems there are various 3dl formats */
static int parse_3dl(AVFilterContext *ctx, LUT3DContext *lut3d, FILE *f)
{
    char line[MAX_LINE_SIZE];
    int ret, i, j, k;
    const int size = 17;
    const int size2 = 17 * 17;
    const float scale = 16*16*16;

    lut3d->lutsize = size;

    ret = ff_allocate_3dlut(ctx, lut3d, size, 0);
    if (ret < 0)
        return ret;

    NEXT_LINE(skip_line(line));
    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                int r, g, b;
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];

                NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%d %d %d", &r, &g, &b) != 3)
                    return AVERROR_INVALIDDATA;
                vec->r = r / scale;
                vec->g = g / scale;
                vec->b = b / scale;
            }
        }
    }
    return 0;
}

/* Pandora format */
static int parse_m3d(AVFilterContext *ctx, LUT3DContext *lut3d, FILE *f)
{
    float scale;
    int ret, i, j, k, size, size2, in = -1, out = -1;
    char line[MAX_LINE_SIZE];
    uint8_t rgb_map[3] = {0, 1, 2};

    while (fgets(line, sizeof(line), f)) {
        if      (!strncmp(line, "in",  2)) in  = strtol(line + 2, NULL, 0);
        else if (!strncmp(line, "out", 3)) out = strtol(line + 3, NULL, 0);
        else if (!strncmp(line, "values", 6)) {
            const char *p = line + 6;
#define SET_COLOR(id) do {                  \
    while (av_isspace(*p))                  \
        p++;                                \
    switch (*p) {                           \
    case 'r': rgb_map[id] = 0; break;       \
    case 'g': rgb_map[id] = 1; break;       \
    case 'b': rgb_map[id] = 2; break;       \
    }                                       \
    while (*p && !av_isspace(*p))           \
        p++;                                \
} while (0)
            SET_COLOR(0);
            SET_COLOR(1);
            SET_COLOR(2);
            break;
        }
    }

    if (in == -1 || out == -1) {
        av_log(ctx, AV_LOG_ERROR, "in and out must be defined\n");
        return AVERROR_INVALIDDATA;
    }
    if (in < 2 || out < 2 ||
        in  > MAX_LEVEL*MAX_LEVEL*MAX_LEVEL ||
        out > MAX_LEVEL*MAX_LEVEL*MAX_LEVEL) {
        av_log(ctx, AV_LOG_ERROR, "invalid in (%d) or out (%d)\n", in, out);
        return AVERROR_INVALIDDATA;
    }
    for (size = 1; size*size*size < in; size++);
    lut3d->lutsize = size;
    size2 = size * size;

    ret = ff_allocate_3dlut(ctx, lut3d, size, 0);
    if (ret < 0)
        return ret;

    scale = 1. / (out - 1);

    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];
                float val[3];

                NEXT_LINE(0);
                if (av_sscanf(line, "%f %f %f", val, val + 1, val + 2) != 3)
                    return AVERROR_INVALIDDATA;
                vec->r = val[rgb_map[0]] * scale;
                vec->g = val[rgb_map[1]] * scale;
                vec->b = val[rgb_map[2]] * scale;
            }
        }
    }
    return 0;
}

static int nearest_sample_index(float *data, float x, int low, int hi)
{
    int mid;
    if (x < data[low])
        return low;

    if (x > data[hi])
        return hi;

    for (;;) {
        av_assert0(x >= data[low]);
        av_assert0(x <= data[hi]);
        av_assert0((hi-low) > 0);

        if (hi - low == 1)
            return low;

        mid = (low + hi) / 2;

        if (x < data[mid])
            hi = mid;
        else
            low = mid;
    }

    return 0;
}

#define NEXT_FLOAT_OR_GOTO(value, label)                    \
    if (!fget_next_word(line, sizeof(line) ,f)) {           \
        ret = AVERROR_INVALIDDATA;                          \
        goto label;                                         \
    }                                                       \
    if (av_sscanf(line, "%f", &value) != 1) {               \
        ret = AVERROR_INVALIDDATA;                          \
        goto label;                                         \
    }

static int parse_cinespace(AVFilterContext *ctx, LUT3DContext *lut3d, FILE *f)
{
    char line[MAX_LINE_SIZE];
    float in_min[3]  = {0.0, 0.0, 0.0};
    float in_max[3]  = {1.0, 1.0, 1.0};
    float out_min[3] = {0.0, 0.0, 0.0};
    float out_max[3] = {1.0, 1.0, 1.0};
    int inside_metadata = 0, size, size2;
    int prelut = 0;
    int ret = 0;

    int prelut_sizes[3] = {0, 0, 0};
    float *in_prelut[3]  = {NULL, NULL, NULL};
    float *out_prelut[3] = {NULL, NULL, NULL};

    NEXT_LINE_OR_GOTO(skip_line(line), end);
    if (strncmp(line, "CSPLUTV100", 10)) {
        av_log(ctx, AV_LOG_ERROR, "Not cineSpace LUT format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    NEXT_LINE_OR_GOTO(skip_line(line), end);
    if (strncmp(line, "3D", 2)) {
        av_log(ctx, AV_LOG_ERROR, "Not 3D LUT format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    while (1) {
        NEXT_LINE_OR_GOTO(skip_line(line), end);

        if (!strncmp(line, "BEGIN METADATA", 14)) {
            inside_metadata = 1;
            continue;
        }
        if (!strncmp(line, "END METADATA", 12)) {
            inside_metadata = 0;
            continue;
        }
        if (inside_metadata == 0) {
            int size_r, size_g, size_b;

            for (int i = 0; i < 3; i++) {
                int npoints = strtol(line, NULL, 0);

                if (npoints > 2) {
                    float v,last;

                    if (npoints > PRELUT_SIZE) {
                        av_log(ctx, AV_LOG_ERROR, "Prelut size too large.\n");
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }

                    if (in_prelut[i] || out_prelut[i]) {
                        av_log(ctx, AV_LOG_ERROR, "Invalid file has multiple preluts.\n");
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }

                    in_prelut[i]  = (float*)av_malloc(npoints * sizeof(float));
                    out_prelut[i] = (float*)av_malloc(npoints * sizeof(float));
                    if (!in_prelut[i] || !out_prelut[i]) {
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }

                    prelut_sizes[i] = npoints;
                    in_min[i] = FLT_MAX;
                    in_max[i] = -FLT_MAX;
                    out_min[i] = FLT_MAX;
                    out_max[i] = -FLT_MAX;

                    for (int j = 0; j < npoints; j++) {
                        NEXT_FLOAT_OR_GOTO(v, end)
                        in_min[i] = FFMIN(in_min[i], v);
                        in_max[i] = FFMAX(in_max[i], v);
                        in_prelut[i][j] = v;
                        if (j > 0 && v < last) {
                            av_log(ctx, AV_LOG_ERROR, "Invalid file, non increasing prelut.\n");
                            ret = AVERROR(ENOMEM);
                            goto end;
                        }
                        last = v;
                    }

                    for (int j = 0; j < npoints; j++) {
                        NEXT_FLOAT_OR_GOTO(v, end)
                        out_min[i] = FFMIN(out_min[i], v);
                        out_max[i] = FFMAX(out_max[i], v);
                        out_prelut[i][j] = v;
                    }

                } else if (npoints == 2)  {
                    NEXT_LINE_OR_GOTO(skip_line(line), end);
                    if (av_sscanf(line, "%f %f", &in_min[i], &in_max[i]) != 2) {
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }
                    NEXT_LINE_OR_GOTO(skip_line(line), end);
                    if (av_sscanf(line, "%f %f", &out_min[i], &out_max[i]) != 2) {
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }

                } else {
                    av_log(ctx, AV_LOG_ERROR, "Unsupported number of pre-lut points.\n");
                    ret = AVERROR_PATCHWELCOME;
                    goto end;
                }

                NEXT_LINE_OR_GOTO(skip_line(line), end);
            }

            if (av_sscanf(line, "%d %d %d", &size_r, &size_g, &size_b) != 3) {
                ret = AVERROR(EINVAL);
                goto end;
            }
            if (size_r != size_g || size_r != size_b) {
                av_log(ctx, AV_LOG_ERROR, "Unsupported size combination: %dx%dx%d.\n", size_r, size_g, size_b);
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }

            size = size_r;
            size2 = size * size;

            if (prelut_sizes[0] && prelut_sizes[1] && prelut_sizes[2])
                prelut = 1;

            ret = ff_allocate_3dlut(ctx, lut3d, size, prelut);
            if (ret < 0)
                return ret;

            for (int k = 0; k < size; k++) {
                for (int j = 0; j < size; j++) {
                    for (int i = 0; i < size; i++) {
                        struct rgbvec *vec = &lut3d->lut[i * size2 + j * size + k];

                        NEXT_LINE_OR_GOTO(skip_line(line), end);
                        if (av_sscanf(line, "%f %f %f", &vec->r, &vec->g, &vec->b) != 3) {
                            ret = AVERROR_INVALIDDATA;
                            goto end;
                        }

                        vec->r *= out_max[0] - out_min[0];
                        vec->g *= out_max[1] - out_min[1];
                        vec->b *= out_max[2] - out_min[2];
                    }
                }
            }

            break;
        }
    }

    if (prelut) {
        for (int c = 0; c < 3; c++) {

            lut3d->prelut.min[c] = in_min[c];
            lut3d->prelut.max[c] = in_max[c];
            lut3d->prelut.scale[c] =  (1.0f / (float)(in_max[c] - in_min[c])) * (lut3d->prelut.size - 1);

            for (int i = 0; i < lut3d->prelut.size; ++i) {
                float mix = (float) i / (float)(lut3d->prelut.size - 1);
                float x = lerpf(in_min[c], in_max[c], mix), a, b;

                int idx = nearest_sample_index(in_prelut[c], x, 0, prelut_sizes[c]-1);
                av_assert0(idx + 1 < prelut_sizes[c]);

                a   = out_prelut[c][idx + 0];
                b   = out_prelut[c][idx + 1];
                mix = x - in_prelut[c][idx];

                lut3d->prelut.lut[c][i] = sanitizef(lerpf(a, b, mix));
            }
        }
        lut3d->scale.r = 1.00f;
        lut3d->scale.g = 1.00f;
        lut3d->scale.b = 1.00f;

    } else {
        lut3d->scale.r = av_clipf(1. / (in_max[0] - in_min[0]), 0.f, 1.f);
        lut3d->scale.g = av_clipf(1. / (in_max[1] - in_min[1]), 0.f, 1.f);
        lut3d->scale.b = av_clipf(1. / (in_max[2] - in_min[2]), 0.f, 1.f);
    }

end:
    for (int c = 0; c < 3; c++) {
        av_freep(&in_prelut[c]);
        av_freep(&out_prelut[c]);
    }
    return ret;
}

av_cold int ff_lut3d_init_using_reader(AVFilterContext *ctx, LUT3DContext *lut3d,
                                       const char* lut_type, const char* text, size_t text_len)
{
    int ret;
    LUTReader reader;

    lut3d->scale.r = lut3d->scale.g = lut3d->scale.b = 1.f;

    if (text_len == 0) {
        return set_identity_matrix(ctx, lut3d, 32);
    }

    lut_reader_init(&reader, text, text_len);

    if (!av_strcasecmp(lut_type, "dat")) {
        ret = parse_dat(ctx, lut3d, f);
    } else if (!av_strcasecmp(lut_type, "3dl")) {
        ret = parse_3dl(ctx, lut3d, f);
    } else if (!av_strcasecmp(lut_type, "cube")) {
        ret = parse_cube(ctx, lut3d, f);
    } else if (!av_strcasecmp(lut_type, "m3d")) {
        ret = parse_m3d(ctx, lut3d, f);
    } else if (!av_strcasecmp(lut_type, "csp")) {
        ret = parse_cinespace(ctx, lut3d, f);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Unrecognized '.%s' LUT lut_type\n", lut_type);
        ret = AVERROR(EINVAL);
    }

    if (!ret && !lut3d->lutsize) {
        av_log(ctx, AV_LOG_ERROR, "3D LUT is empty\n");
        ret = AVERROR_INVALIDDATA;
    }

    return ret;
}

av_cold int ff_lut3d_init(AVFilterContext *ctx, LUT3DContext *lut3d)
{
    int ret;
    FILE *f;
    const char *ext;

    lut3d->scale.r = lut3d->scale.g = lut3d->scale.b = 1.f;

    if (!lut3d->file) {
        return set_identity_matrix(ctx, lut3d, 32);
    }

    f = avpriv_fopen_utf8(lut3d->file, "r");
    if (!f) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "%s: %s\n", lut3d->file, av_err2str(ret));
        return ret;
    }

    ext = strrchr(lut3d->file, '.');
    if (!ext) {
        av_log(ctx, AV_LOG_ERROR, "Unable to guess the format from the extension\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    ext++;

    if (!av_strcasecmp(ext, "dat")) {
        ret = parse_dat(ctx, lut3d, f);
    } else if (!av_strcasecmp(ext, "3dl")) {
        ret = parse_3dl(ctx, lut3d, f);
    } else if (!av_strcasecmp(ext, "cube")) {
        ret = parse_cube(ctx, lut3d, f);
    } else if (!av_strcasecmp(ext, "m3d")) {
        ret = parse_m3d(ctx, lut3d, f);
    } else if (!av_strcasecmp(ext, "csp")) {
        ret = parse_cinespace(ctx, lut3d, f);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Unrecognized '.%s' file type\n", ext);
        ret = AVERROR(EINVAL);
    }

    if (!ret && !lut3d->lutsize) {
        av_log(ctx, AV_LOG_ERROR, "3D LUT is empty\n");
        ret = AVERROR_INVALIDDATA;
    }

end:
    fclose(f);
    return ret;
}

av_cold void ff_lut3d_uninit(LUT3DContext *lut3d)
{
    int i;
    av_freep(&lut3d->lut);

    for (i = 0; i < 3; i++) {
        av_freep(&lut3d->prelut.lut[i]);
    }
}
