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
#ifndef AVFILTER_LUT3D_H
#define AVFILTER_LUT3D_H

#include "config_components.h"

#include "libavutil/pixdesc.h"
#include "framesync.h"
#include "avfilter.h"

enum interp_mode {
    INTERPOLATE_NEAREST,
    INTERPOLATE_TRILINEAR,
    INTERPOLATE_TETRAHEDRAL,
    INTERPOLATE_PYRAMID,
    INTERPOLATE_PRISM,
    NB_INTERP_MODE
};

struct rgbvec {
    float r, g, b;
};

/* 3D LUT don't often go up to level 32, but it is common to have a Hald CLUT
 * of 512x512 (64x64x64) */
#define MAX_LEVEL 256
#define PRELUT_SIZE 65536

typedef struct Lut3DPreLut {
    int size;
    float min[3];
    float max[3];
    float scale[3];
    float* lut[3];
} Lut3DPreLut;

typedef struct LUT3DContext {
    const AVClass *class;
    struct rgbvec *lut;
    int lutsize;
    int lutsize2;
    struct rgbvec scale;
    int interpolation;          ///<interp_mode
    char *lut_text;
    int lut_text_len;
    uint8_t rgba_map[4];
    int step;
    avfilter_action_func *interp;
    Lut3DPreLut prelut;
#if CONFIG_HALDCLUT_FILTER
    int clut;
    int got_clut;
    uint8_t clut_rgba_map[4];
    int clut_step;
    int clut_bits;
    int clut_planar;
    int clut_float;
    int clut_width;
    FFFrameSync fs;
#endif
} LUT3DContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

void ff_lut3d_init_x86(LUT3DContext *s, const AVPixFmtDescriptor *desc);

int ff_allocate_3dlut(AVFilterContext *ctx, LUT3DContext *lut3d, int lutsize, int prelut);

/**
 * Load 3D LUT from file.
 *
 * @param lut3d LUT3DContext Load 3D LUT from path specified by `lut3d->file`.
 *     If `lut3d->file` is NULL, initialize an identity 3D LUT.
 */
int ff_lut3d_init(AVFilterContext *ctx, LUT3DContext *lut3d);

/**  Release memory used to hold 3D LUT. */
void ff_lut3d_uninit(LUT3DContext *lut3d);

#endif /* AVFILTER_LUT3D_H */
