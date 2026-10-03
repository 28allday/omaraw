// SPDX-License-Identifier: GPL-3.0-or-later
// Original bounded readers for OmaRAW profile containers and XMP table data.
// The readers are original GPL-3.0-or-later implementations.
#pragma once
#include "creativeprofilelut.h"
#include <stdint.h>

typedef struct oma_hsv_table { unsigned h,s,v,encoding; float *data; float minimum,maximum; } oma_hsv_table;
typedef struct oma_creative_data {
  oma_cube cube;
  oma_hsv_table hsv;
  unsigned dimensions,primaries,gamma,gamut;
  int enhanced,supports_amount,scene,output;
  float minimum,maximum,rgb_amount,hsv_amount;
} oma_creative_data;
static inline uint32_t oma_profile_u32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static inline float oma_profile_f32(const unsigned char *p)
{ uint32_t bits=oma_profile_u32(p); float v; memcpy(&v,&bits,4); return v; }
static inline double oma_profile_f64(const unsigned char *p)
{ uint64_t bits=(uint64_t)oma_profile_u32(p)|((uint64_t)oma_profile_u32(p+4)<<32); double v; memcpy(&v,&bits,8); return v; }
static inline void oma_creative_clear(oma_creative_data *p)
{ oma_cube_clear(&p->cube); free(p->hsv.data); memset(p,0,sizeof(*p)); }
static inline int oma_profile_amounts(const unsigned char *p,float *low,float *high)
{
  const double a=oma_profile_f64(p),b=oma_profile_f64(p+8);
  if(!isfinite(a) || !isfinite(b) || a<0 || a>1 || b<1 || b>10) return 0;
  *low=(float)a; *high=(float)b; return 1;
}
static inline int oma_creative_rgb(const unsigned char *p,size_t length,oma_creative_data *out)
{
  if(length<44 || oma_profile_u32(p)!=1 || oma_profile_u32(p+4)!=1) return 0;
  const unsigned dim=oma_profile_u32(p+8),n=oma_profile_u32(p+12);
  if((dim!=1 && dim!=3) || n<2 || n>(dim==1 ? 4096u : 64u)) return 0;
  const size_t samples=dim==1 ? n : (size_t)n*n*n, tail=16+samples*6;
  if(length!=tail+28 && length!=tail+32) return 0;
  // Unknown flags are rejected rather than changing rendering silently.
  if(length==tail+32 && oma_profile_u32(p+tail+28)) return 0;
  const unsigned prim=oma_profile_u32(p+tail),gamma=oma_profile_u32(p+tail+4),gamut=oma_profile_u32(p+tail+8);
  if(prim>4 || gamma>4 || gamut>1 || !oma_profile_amounts(p+tail+12,&out->minimum,&out->maximum)) return 0;
  float *values=(float *)malloc(samples*3*sizeof(float)); if(!values) return 0;
  // Table samples are unsigned modular differences from a quantized identity.
  // XMP tables are blue-fastest; CUBE storage is red-fastest.
  for(size_t i=0;i<samples;++i) {
    const unsigned axis[3]={dim==1 ? (unsigned)i : (unsigned)(i/(n*n)),
                            dim==1 ? (unsigned)i : (unsigned)((i/n)%n),
                            dim==1 ? (unsigned)i : (unsigned)(i%n)};
    const size_t dest=dim==1 ? i : ((size_t)axis[2]*n+axis[1])*n+axis[0];
    for(int c=0;c<3;++c) {
      const unsigned identity=(axis[c]*65535u+n/2)/(n-1);
      const unsigned char *v=p+16+6*i+2*c;
      values[3*dest+c]=(float)((identity+(unsigned)v[0]+((unsigned)v[1]<<8))&65535u)/65535.f;
    }
  }
  out->cube.size=(int)n; out->cube.rgb=values;
  for(int c=0;c<3;++c) { out->cube.lo[c]=0; out->cube.hi[c]=1; }
  out->dimensions=dim; out->primaries=prim; out->gamma=gamma; out->gamut=gamut;
  return 1;
}
static inline int oma_creative_hsv(const unsigned char *p,size_t length,oma_hsv_table *out)
{
  if(length<24 || oma_profile_u32(p)!=0) return 0;
  const unsigned version=oma_profile_u32(p+4),h=oma_profile_u32(p+8),s=oma_profile_u32(p+12),v=oma_profile_u32(p+16);
  if((version!=1 && version!=2) || !h || !s || !v || h>360 || s>256 || v>256) return 0;
  const uint64_t count=(uint64_t)h*s*v,tail=20+count*12,expected=tail+4+(version==2 ? 16 : 0);
  if(count>1024*1024 || (length!=expected && length!=expected+4)) return 0;
  if(length==expected+4 && oma_profile_u32(p+expected)) return 0;
  const unsigned enc=oma_profile_u32(p+tail); if(enc>1) return 0;
  float low=1,high=1;
  if(version==2 && !oma_profile_amounts(p+tail+4,&low,&high)) return 0;
  float *data=(float *)malloc((size_t)count*3*sizeof(float)); if(!data) return 0;
  for(size_t i=0;i<count*3;++i) {
    data[i]=oma_profile_f32(p+20+i*4);
    if(!isfinite(data[i]) || (i%3 ? data[i]<0 || data[i]>100 : fabsf(data[i])>360)) { free(data); return 0; }
  }
  out->h=h; out->s=s; out->v=v; out->encoding=enc; out->minimum=low; out->maximum=high; out->data=data; return 1;
}
// Native envelope: magic, flags, RGB/HSV default amounts, two byte lengths,
// then the independently validated RGB and/or HSV binary table payloads.
static inline int oma_creative_parse(const char *bytes,size_t length,oma_creative_data *out)
{
  oma_creative_data p={0}; p.supports_amount=1; p.minimum=0; p.maximum=2; p.rgb_amount=p.hsv_amount=1;
  if(length<8 || memcmp(bytes,"OMAPRF01",8)) {
    if(!oma_cube_parse(bytes,length,&p.cube)) return 0;
    p.dimensions=3; *out=p; return 1;
  }
  if(length<28 || length>OMA_CUBE_MAX_BYTES) return 0;
  const unsigned char *b=(const unsigned char *)bytes;
  const uint32_t flags=oma_profile_u32(b+8),rgb=oma_profile_u32(b+20),hsv=oma_profile_u32(b+24);
  p.rgb_amount=oma_profile_f32(b+12); p.hsv_amount=oma_profile_f32(b+16);
  if(flags>7 || !(flags&6) || (!rgb && !hsv) || (uint64_t)28+rgb+hsv!=length
     || !isfinite(p.rgb_amount) || p.rgb_amount<0 || p.rgb_amount>10
     || !isfinite(p.hsv_amount) || p.hsv_amount<0 || p.hsv_amount>10) return 0;
  if((rgb && !oma_creative_rgb(b+28,rgb,&p)) || (hsv && !oma_creative_hsv(b+28+rgb,hsv,&p.hsv))) {
    oma_creative_clear(&p); return 0;
  }
  p.enhanced=1; p.supports_amount=flags&1; p.scene=flags&2; p.output=flags&4; *out=p; return 1;
}
static inline int oma_creative_read(const char *path,oma_creative_data *out)
{
  FILE *f=fopen(path,"rb"); if(!f) return 0;
  if(fseek(f,0,SEEK_END)) { fclose(f); return 0; }
  const long length=ftell(f);
  if(length<=0 || length>OMA_CUBE_MAX_BYTES || fseek(f,0,SEEK_SET)) { fclose(f); return 0; }
  char *bytes=(char *)malloc((size_t)length);
  const int read=bytes && fread(bytes,1,(size_t)length,f)==(size_t)length && fgetc(f)==EOF;
  fclose(f); const int valid=read && oma_creative_parse(bytes,(size_t)length,out); free(bytes); return valid;
}
static inline float oma_creative_amount(float ui,float low,float mid,float high,int supported)
{
  // The stored default is clamped to the table's own allowed range.
  mid=oma_profile_clamp(mid,low,high);
  if(!supported) return mid;
  ui=oma_profile_clamp(ui,0,200)/100;
  return ui<=1 ? low+(mid-low)*ui : mid+(high-mid)*(ui-1);
}
static inline float oma_profile_encode(float x,unsigned gamma)
{
  x=fmaxf(0,x);
  switch(gamma) {
    case 1: return x<=.0031308f ? 12.92f*x : 1.055f*powf(x,1/2.4f)-.055f;
    case 2: return powf(x,1/1.8f);
    case 3: return powf(x,1/2.2f);
    case 4: return x<.01805397f ? 4.5f*x : 1.09929683f*powf(x,.45f)-.09929683f;
    default: return x;
  }
}
static inline float oma_profile_decode(float x,unsigned gamma)
{
  x=fmaxf(0,x);
  switch(gamma) {
    case 1: return x<=.04045f ? x/12.92f : powf((x+.055f)/1.055f,2.4f);
    case 2: return powf(x,1.8f);
    case 3: return powf(x,2.2f);
    case 4: return x<4.5f*.01805397f ? x/4.5f : powf((x+.09929683f)/1.09929683f,1/.45f);
    default: return x;
  }
}
static inline void oma_creative_rgb_pixel(const oma_creative_data *p,const float in[3],float out[3],float amount)
{
  float encoded[3],styled[3],residual[3];
  for(int c=0;c<3;++c) {
    const float bounded=oma_profile_clamp(in[c],0,1);
    residual[c]=p->gamut ? in[c]-bounded : 0;
    encoded[c]=oma_profile_encode(bounded,p->gamma);
  }
  if(p->dimensions==3) oma_cube_pixel(&p->cube,encoded,styled);
  else for(int c=0;c<3;++c) {
    const float pos=encoded[c]*(p->cube.size-1); const int low=(int)fminf(floorf(pos),p->cube.size-2);
    const float a=p->cube.rgb[3*low+c],b=p->cube.rgb[3*(low+1)+c]; styled[c]=a+(pos-low)*(b-a);
  }
  for(int c=0;c<3;++c)
    out[c]=oma_profile_decode(oma_profile_clamp(encoded[c]+amount*(styled[c]-encoded[c]),0,1),p->gamma)+residual[c];
}
static inline void oma_creative_hsv_pixel(const oma_hsv_table *t,const float in[3],float out[3],float amount)
{
  // DNG look-table coordinates: value-major, hue next, saturation fastest.
  float rgb[3]; for(int c=0;c<3;++c) rgb[c]=fmaxf(0,in[c]);
  const float v=fmaxf(rgb[0],fmaxf(rgb[1],rgb[2])),lo=fminf(rgb[0],fminf(rgb[1],rgb[2])),delta=v-lo;
  if(v<=0) { memcpy(out,in,3*sizeof(float)); return; }
  float h=0,s=delta/v;
  if(delta>0) {
    h=rgb[0]==v ? (rgb[1]-rgb[2])/delta : rgb[1]==v ? 2+(rgb[2]-rgb[0])/delta : 4+(rgb[0]-rgb[1])/delta;
    if(h<0) h+=6;
  }
  const float ve=oma_profile_encode(fminf(v,1),t->encoding);
  const float pos[3]={h*t->h/6,s*(t->s-1),ve*(t->v-1)};
  unsigned a[3],b[3]; float f[3]; const unsigned dims[3]={t->h,t->s,t->v};
  for(int c=0;c<3;++c) { a[c]=(unsigned)fminf(floorf(pos[c]),dims[c]-1); b[c]=a[c]+1<dims[c] ? a[c]+1 : c==0 ? 0 : a[c]; f[c]=pos[c]-a[c]; }
  float mod[3]={0};
  for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
    const size_t index=(((size_t)(z ? b[2] : a[2])*t->h+(x ? b[0] : a[0]))*t->s+(y ? b[1] : a[1]))*3;
    const float weight=(x ? f[0] : 1-f[0])*(y ? f[1] : 1-f[1])*(z ? f[2] : 1-f[2]);
    for(int c=0;c<3;++c) mod[c]+=weight*t->data[index+c];
  }
  h=fmodf(h+amount*mod[0]/60,6); if(h<0) h+=6; s=oma_profile_clamp(s*(1+amount*(mod[1]-1)),0,1);
  const float scale=fmaxf(0,1+amount*(mod[2]-1));
  const float value=t->encoding && ve>0 ? v*oma_profile_decode(ve*scale,t->encoding)/fminf(v,1) : v*scale;
  const float chroma=value*s,secondary=chroma*(1-fabsf(fmodf(h,2)-1)),base=value-chroma;
  const float components[6][3]={{chroma,secondary,0},{secondary,chroma,0},{0,chroma,secondary},
                                {0,secondary,chroma},{secondary,0,chroma},{chroma,0,secondary}};
  for(int c=0;c<3;++c) out[c]=components[(int)h][c]+base+(in[c]-rgb[c]);
}
