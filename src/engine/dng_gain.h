// SPDX-License-Identifier: GPL-3.0-or-later
// Independent implementation of DNG 1.7.1 ProfileGainTableMap(2).
// This product includes DNG technology under license by Adobe.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OMA_DNG_GAIN_LIMIT (16u*1024u*1024u)
typedef struct oma_dng_gain {
  uint32_t rows, cols, points;
  double spacing[2], origin[2];
  float weights[5], gamma, baseline;
  float *values;
  uint32_t width, height, active[4];
} oma_dng_gain;
static inline void oma_dng_gain_clear(oma_dng_gain *m)
{ free(m->values); memset(m,0,sizeof(*m)); }
static inline uint16_t oma_dng_u16(const unsigned char *p,int le)
{ return le ? (uint16_t)(p[0]|p[1]<<8) : (uint16_t)(p[1]|p[0]<<8); }
static inline uint32_t oma_dng_u32(const unsigned char *p,int le)
{ return le ? (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24
            : (uint32_t)p[3]|(uint32_t)p[2]<<8|(uint32_t)p[1]<<16|(uint32_t)p[0]<<24; }
static inline float oma_dng_f32(const unsigned char *p,int le)
{ const uint32_t u=oma_dng_u32(p,le); float v; memcpy(&v,&u,4); return v; }
static inline double oma_dng_f64(const unsigned char *p,int le)
{
  const uint64_t u=le ? (uint64_t)oma_dng_u32(p,le)|((uint64_t)oma_dng_u32(p+4,le)<<32)
                      : (uint64_t)oma_dng_u32(p+4,le)|((uint64_t)oma_dng_u32(p,le)<<32);
  double v; memcpy(&v,&u,8); return v;
}
static inline float oma_dng_f16(uint16_t u)
{
  const int e=(u>>10)&31, f=u&1023;
  const float v=e==31 ? (f ? NAN : INFINITY) : e ? ldexpf(1.f+f/1024.f,e-15) : ldexpf((float)f,-24);
  return u&0x8000 ? -v : v;
}
// The caller supplies an empty map. All dimensions, products and floats are
// checked before allocation; arbitrary TIFF payloads never become array sizes.
static inline int oma_dng_gain_parse(const unsigned char *p,size_t size,int le,int version,oma_dng_gain *map)
{
  const size_t header=version==2 ? 80 : 64;
  if(!p || size<header || size>OMA_DNG_GAIN_LIMIT || (version!=1 && version!=2)) return 0;
  oma_dng_gain m={0}; m.gamma=1; m.baseline=1;
  m.rows=oma_dng_u32(p,le); m.cols=oma_dng_u32(p+4,le); m.points=oma_dng_u32(p+40,le);
  if(!m.rows || !m.cols || !m.points || m.rows>4096 || m.cols>4096 || m.points>65536) return 0;
  const uint64_t count=(uint64_t)m.rows*m.cols*m.points;
  const uint32_t type=version==2 ? oma_dng_u32(p+64,le) : 3;
  const size_t stride=type==0 ? 1 : type==3 ? 4 : 2;
  if(type>3 || count>OMA_DNG_GAIN_LIMIT/4 || size!=header+count*stride) return 0;
  for(int i=0;i<2;++i) {
    m.spacing[i]=oma_dng_f64(p+8+8*i,le); m.origin[i]=oma_dng_f64(p+24+8*i,le);
    if(!isfinite(m.spacing[i]) || m.spacing[i]<=0 || !isfinite(m.origin[i])) return 0;
  }
  for(int i=0;i<5;++i) { m.weights[i]=oma_dng_f32(p+44+4*i,le); if(!isfinite(m.weights[i])) return 0; }
  float low=0, high=0;
  if(version==2) {
    m.gamma=oma_dng_f32(p+68,le);
    if(!isfinite(m.gamma) || m.gamma<.25f || m.gamma>4) return 0;
    if(type<2) {
      low=oma_dng_f32(p+72,le); high=oma_dng_f32(p+76,le);
      if(!isfinite(low) || !isfinite(high) || low<0 || high<low) return 0;
    }
  }
  m.values=(float *)malloc((size_t)count*sizeof(float)); if(!m.values) return 0;
  for(size_t i=0;i<count;++i) {
    const unsigned char *v=p+header+i*stride;
    const float gain=type==0 ? low+(high-low)*(v[0]/255.f)
                    : type==1 ? low+(high-low)*(oma_dng_u16(v,le)/65535.f)
                    : type==2 ? oma_dng_f16(oma_dng_u16(v,le)) : oma_dng_f32(v,le);
    if(!isfinite(gain) || gain<0) { oma_dng_gain_clear(&m); return 0; }
    m.values[i]=gain;
  }
  *map=m; return 1;
}
static inline float oma_dng_gain_at(const oma_dng_gain *m,double x,double y,const float rgb[3])
{
  if(!m->values) return 1;
  const float low=fminf(rgb[0],fminf(rgb[1],rgb[2])), high=fmaxf(rgb[0],fmaxf(rgb[1],rgb[2]));
  const double input=(double)rgb[0]*m->weights[0]+(double)rgb[1]*m->weights[1]+(double)rgb[2]*m->weights[2]
                     +(double)low*m->weights[3]+(double)high*m->weights[4];
  // DNG specifies N, not N-1, for the intensity coordinate. Clamp edge tables.
  const double pos[3]={fmin(fmax((y-m->origin[0])/m->spacing[0],0),m->rows-1),
                       fmin(fmax((x-m->origin[1])/m->spacing[1],0),m->cols-1),
                       fmin(pow(fmin(fmax(input,0),1),m->gamma)*m->points,m->points-1)};
  uint32_t a[3],b[3]; double t[3]; const uint32_t dim[3]={m->rows,m->cols,m->points};
  for(int i=0;i<3;++i) { a[i]=(uint32_t)pos[i]; b[i]=a[i]+1<dim[i] ? a[i]+1 : a[i]; t[i]=pos[i]-a[i]; }
  double gain=0;
  for(int v=0;v<2;++v) for(int h=0;h<2;++h) for(int n=0;n<2;++n) {
    const size_t index=((size_t)(v ? b[0] : a[0])*m->cols+(h ? b[1] : a[1]))*m->points+(n ? b[2] : a[2]);
    gain+=(v ? t[0] : 1-t[0])*(h ? t[1] : 1-t[1])*(n ? t[2] : 1-t[2])*m->values[index];
  }
  return (float)gain;
}

typedef struct oma_dng_tiff { FILE *file; uint64_t size; int le; } oma_dng_tiff;
static inline int oma_dng_read_at(oma_dng_tiff *f,uint64_t offset,void *out,size_t size)
{ return offset<=f->size && size<=f->size-offset && !fseek(f->file,(long)offset,SEEK_SET) && fread(out,1,size,f->file)==size; }
// Classic TIFF/DNG only; do not interpret JPEGs or arbitrary renamed files.
// Walk bounded image IFDs, never Exif/MakerNote offsets. The active embedded
// camera profile is IFD0; optional alternative camera profiles are not selected.
// Return 1=map, 2=JPEG XL baseline only, 0=no correction, -1=malformed metadata.
static inline int oma_dng_gain_read(const char *path,oma_dng_gain *map)
{
  oma_dng_tiff f={0}; f.file=fopen(path,"rb"); if(!f.file) return 0;
  unsigned char head[8]; int result=0;
  if(fseek(f.file,0,SEEK_END) || ftell(f.file)<8) goto done;
  f.size=(uint64_t)ftell(f.file);
  if(!oma_dng_read_at(&f,0,head,8)) goto done;
  if(!memcmp(head,"II",2)) f.le=1; else if(memcmp(head,"MM",2)) goto done;
  if(oma_dng_u16(head+2,f.le)!=42) goto done;
  {
    uint32_t queue[64]={oma_dng_u32(head+4,f.le)}, visited[64]={0}; unsigned count=1,seen=0;
    uint32_t bestOffset=0,bestSize=0; int bestRank=0,bestVersion=0,dng=0,jxl=0;
    float baseline=1; uint32_t rawWidth=0,rawHeight=0,active[4]={0};
    for(unsigned q=0;q<count;++q) {
      const uint32_t off=queue[q]; if(!off) continue;
      for(unsigned i=0;i<seen;++i) if(visited[i]==off) { result=-1; goto done; }
      visited[seen++]=off;
      unsigned char nb[2]; if(!oma_dng_read_at(&f,off,nb,2)) { result=-1; goto done; }
      const uint32_t n=oma_dng_u16(nb,f.le);
      if(n>4096 || (uint64_t)off+2+12*n+4>f.size) { result=-1; goto done; }
      uint32_t width=0,height=0,photo=0,subfile=0,area[4]={0};
      for(uint32_t i=0;i<n;++i) {
        unsigned char e[12]; if(!oma_dng_read_at(&f,(uint64_t)off+2+12*i,e,12)) { result=-1; goto done; }
        const uint32_t tag=oma_dng_u16(e,f.le),type=oma_dng_u16(e+2,f.le),num=oma_dng_u32(e+4,f.le),value=oma_dng_u32(e+8,f.le);
        if(num==1 && (type==3 || type==4)) {
          const uint32_t scalar=type==3 ? oma_dng_u16(e+8,f.le) : value;
          if(tag==256) width=scalar;
          if(tag==257) height=scalar;
          if(tag==262) photo=scalar;
          if(tag==254) subfile=scalar;
          if(tag==259 && scalar==52546) jxl=1;
        }
        if(tag==50829 && type==4 && num==4) {
          unsigned char a[16]; if(!oma_dng_read_at(&f,value,a,16)) { result=-1; goto done; }
          for(int k=0;k<4;++k) area[k]=oma_dng_u32(a+4*k,f.le);
        }
        if(q==0 && tag==50706 && type==1 && num==4) dng=1;
        if(q==0 && tag==50730 && type==10 && num==1) {
          unsigned char b[8]; if(!oma_dng_read_at(&f,value,b,8)) { result=-1; goto done; }
          const int32_t den=(int32_t)oma_dng_u32(b+4,f.le), nom=(int32_t)oma_dng_u32(b,f.le);
          if(!den || fabs((double)nom/den)>16) { result=-1; goto done; }
          baseline=exp2f((float)nom/den);
        }
        if(tag==330 && (type==4 || type==13)) {
          if(num>64-count) { result=-1; goto done; }
          for(uint32_t j=0;j<num;++j) {
            unsigned char b[4];
            if(num==1) memcpy(b,e+8,4);
            else if(!oma_dng_read_at(&f,(uint64_t)value+4*j,b,4)) { result=-1; goto done; }
            queue[count++]=oma_dng_u32(b,f.le);
          }
        }
        if(tag==52525 || (tag==52544 && q==0)) {
          const int rank=tag==52544 ? 3 : q==0 ? 2 : 1;
          if(type!=7 || num<64 || num>OMA_DNG_GAIN_LIMIT || (uint64_t)value+num>f.size) { result=-1; goto done; }
          if(rank>bestRank) { bestRank=rank; bestOffset=value; bestSize=num; bestVersion=tag==52544 ? 2 : 1; }
        }
      }
      if(!rawWidth && !subfile && (photo==32803 || photo==34892) && width && height) {
        rawWidth=width; rawHeight=height;
        if(!area[2] && !area[3]) { area[2]=height; area[3]=width; }
        if(area[0]>=area[2] || area[1]>=area[3] || area[2]>height || area[3]>width) { result=-1; goto done; }
        memcpy(active,area,sizeof(active));
      }
    }
    if(dng && bestRank) {
      unsigned char *data=(unsigned char *)malloc(bestSize);
      result=data && oma_dng_read_at(&f,bestOffset,data,bestSize)
                 && oma_dng_gain_parse(data,bestSize,f.le,bestVersion,map) ? 1 : -1;
      free(data); if(result==1) {
        map->baseline=baseline; map->width=rawWidth; map->height=rawHeight;
        memcpy(map->active,active,sizeof(active));
        if(!rawWidth) { oma_dng_gain_clear(map); result=-1; }
      }
    } else if(dng && jxl && rawWidth) {
      map->baseline=baseline; map->width=rawWidth; map->height=rawHeight;
      memcpy(map->active,active,sizeof(active)); result=2;
    }
  }
done:
  fclose(f.file); return result;
}
