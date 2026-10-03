// SPDX-License-Identifier: GPL-3.0-or-later
// OmaRAW's original creative looks and bounded CUBE reader. Shared by the
// importer, CPU engine and tests.
#pragma once
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OMA_CUBE_MAX_BYTES (32 * 1024 * 1024)
typedef struct oma_cube { int size; float lo[3], hi[3]; float *rgb; } oma_cube;
static inline void oma_cube_clear(oma_cube *c) { free(c->rgb); memset(c, 0, sizeof(*c)); }
static inline float oma_profile_clamp(float x, float lo, float hi) { return fmaxf(lo, fminf(hi, x)); }
static inline int oma_cube_number(const char *s, float *out)
{
  char *end = NULL; const double v = g_ascii_strtod(s, &end);
  if(end == s || *end || !isfinite(v) || fabs(v) > 65536.) return 0;
  *out = (float)v; return 1;
}

// The caller starts with an empty cube. Reject truncated/extra rows, unknown
// directives, 1D shapers, duplicate declarations and non-finite values.
static inline int oma_cube_parse(const char *data, size_t length, oma_cube *cube)
{
  if(!data || !length || length > OMA_CUBE_MAX_BYTES || memchr(data, 0, length)) return 0;
  oma_cube c = {0, {0,0,0}, {1,1,1}, NULL};
  char *copy = g_strndup(data, length), *line = copy;
  size_t count = 0; int valid = 1, low = 0, high = 0;
  while(line && valid)
  {
    char *next = strchr(line, '\n'); if(next) *next++ = 0;
    char *comment = strchr(line, '#'); if(comment) *comment = 0;
    g_strstrip(line);
    if(*line)
    {
      gchar **split = g_strsplit_set(line, " \t\r", -1);
      const char *tokens[5]; int n = 0;
      for(int i = 0; split[i]; ++i) if(*split[i]) { if(n < 5) tokens[n] = split[i]; ++n; }
      if(!strcmp(tokens[0], "TITLE")) { /* descriptive text is not executable */ }
      else if(!strcmp(tokens[0], "LUT_3D_SIZE"))
      {
        float size = 0;
        valid = n == 2 && !c.size && !count && oma_cube_number(tokens[1], &size)
             && size >= 2 && size <= 64 && size == floorf(size);
        if(valid) { c.size = (int)size; c.rgb = (float *)calloc((size_t)c.size*c.size*c.size*3, sizeof(float)); valid = c.rgb != NULL; }
      }
      else if(!strcmp(tokens[0], "DOMAIN_MIN") || !strcmp(tokens[0], "DOMAIN_MAX"))
      {
        const int minimum = !strcmp(tokens[0], "DOMAIN_MIN");
        valid = n == 4 && !count && !(minimum ? low : high);
        for(int k = 0; k < 3 && valid; ++k) valid = oma_cube_number(tokens[k+1], minimum ? c.lo+k : c.hi+k);
        if(minimum) low = 1; else high = 1;
      }
      else
      {
        valid = n == 3 && c.rgb && count < (size_t)c.size*c.size*c.size;
        for(int k = 0; k < 3 && valid; ++k) valid = oma_cube_number(tokens[k], c.rgb+3*count+k);
        if(valid) ++count;
      }
      g_strfreev(split);
    }
    line = next;
  }
  g_free(copy);
  valid &= c.size > 0 && count == (size_t)c.size*c.size*c.size;
  for(int k = 0; k < 3; ++k) valid &= c.hi[k] - c.lo[k] >= 1e-6f;
  if(!valid) { oma_cube_clear(&c); return 0; }
  *cube = c; return 1;
}

static inline int oma_cube_read(const char *path, oma_cube *cube)
{
  FILE *f = fopen(path, "rb"); if(!f) return 0;
  if(fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
  const long length = ftell(f);
  if(length <= 0 || length > OMA_CUBE_MAX_BYTES || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
  char *data = (char *)malloc((size_t)length);
  const int read = data && fread(data, 1, (size_t)length, f) == (size_t)length && fgetc(f) == EOF;
  fclose(f);
  const int valid = read && oma_cube_parse(data, (size_t)length, cube);
  free(data); return valid;
}

// Red-fastest CUBE ordering, trilinear interpolation. Preserve excursion
// outside the domain as an additive residual, rather than clipping HDR input.
static inline void oma_cube_pixel(const oma_cube *c, const float in[3], float out[3])
{
  int index[3]; float fraction[3], residual[3];
  for(int k = 0; k < 3; ++k)
  {
    const float bounded = oma_profile_clamp(in[k], c->lo[k], c->hi[k]);
    const float pos = (bounded-c->lo[k])/(c->hi[k]-c->lo[k])*(c->size-1);
    index[k] = (int)fminf(floorf(pos), c->size-2); fraction[k] = pos-index[k];
    residual[k] = in[k]-bounded; out[k] = 0;
  }
  for(int b = 0; b < 2; ++b) for(int g = 0; g < 2; ++g) for(int r = 0; r < 2; ++r)
  {
    const float w = (r ? fraction[0] : 1-fraction[0])*(g ? fraction[1] : 1-fraction[1])*(b ? fraction[2] : 1-fraction[2]);
    const size_t p = 3*((size_t)(index[2]+b)*c->size*c->size+(index[1]+g)*c->size+index[0]+r);
    for(int k = 0; k < 3; ++k) out[k] += w*c->rgb[p+k];
  }
  for(int k = 0; k < 3; ++k) out[k] += residual[k];
}

// Fixed v1 recipes, evaluated in encoded sRGB. IDs are persistent history
// values: keep these recipes unchanged when adding future looks.
static inline void oma_profile_builtin(int look, const float in[3], float out[3])
{
  float x[3]; for(int k=0;k<3;++k) x[k]=oma_profile_clamp(in[k],0,1);
  const float y=.2126f*x[0]+.7152f*x[1]+.0722f*x[2];
  float saturation=1, contrast=0, fade=0, shadows[3]={0}, highlights[3]={0};
  switch(look)
  {
    case 1: saturation=.92f; contrast=.10f; shadows[0]=.008f; highlights[0]=.024f; highlights[2]=-.020f; break; // warm portrait
    case 2: saturation=1.12f; contrast=.18f; shadows[2]=.014f; break; // clear landscape
    case 3: saturation=.84f; contrast=-.12f; fade=.028f; shadows[2]=.014f; highlights[0]=.018f; break; // soft colour
    case 4: saturation=.88f; contrast=.16f; shadows[0]=-.020f; shadows[1]=.014f; shadows[2]=.022f; highlights[0]=.022f; highlights[2]=-.018f; break; // cinema dusk
    case 5: saturation=0; contrast=.24f; break; // silver monochrome
    case 6: saturation=0; contrast=.08f; fade=.020f; shadows[0]=.014f; shadows[2]=-.012f; highlights[0]=.016f; highlights[2]=-.014f; break; // warm monochrome
    default: memcpy(out,in,3*sizeof(float)); return;
  }
  for(int k=0;k<3;++k)
  {
    float v=y+saturation*(x[k]-y);
    const float t=oma_profile_clamp(v,0,1);
    v += contrast*(t-.5f)*4*t*(1-t) + fade*(1-t)*(1-t);
    v += 4*y*(1-y)*(shadows[k]*(1-y)+highlights[k]*y);
    // Monochrome preserves out-of-range luminance, not channel differences.
    const float residual=look==5 || look==6 ? .2126f*(in[0]-x[0])+.7152f*(in[1]-x[1])+.0722f*(in[2]-x[2]) : in[k]-x[k];
    out[k]=v+residual;
  }
}
