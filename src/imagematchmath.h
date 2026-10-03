// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// Version-four native-history payload. The first 34 floats retain v3 layout.
// All sizes are in pixels of a 3000px
// long-edge photograph, so a reference and a different-resolution RAW agree
// at the same viewing/output size. Never reinterpret a future payload as v1.
typedef struct oma_match_params {
  float exposure, warmth, tint;
  float exposure_on, wb_on, tone_strength, colour_strength, grain_strength;
  float tone[9];                  // offsets from a straight encoded-luma curve
  float colour_a[3], colour_b[3]; // Oklab chroma offsets, shadows/mids/highlights
  float colour_scale;
  float grain_amount, grain_size, grain_roughness, grain_colour;
  float grain_shadows, grain_midtones, grain_highlights;
  float protect_skin;
  float grain_texture;           // 0: original grid; 1: decorrelated organic field
  float render_version;         // preserve saved colour and grain behaviour
  float tone_on, colour_on, grain_on; // mute components without losing strengths
} oma_match_params;

static inline float oma_match_clamp(float x, float a, float b) { return fminf(b, fmaxf(a, x)); }
static inline float oma_match_encode(float x) { return x <= .0031308f ? 12.92f*x : 1.055f*powf(x,1.f/2.4f)-.055f; }
static inline float oma_match_decode(float x) { return x <= .04045f ? x/12.92f : powf((x+.055f)/1.055f,2.4f); }
static inline float oma_match_luma(const float *v) { return .2126f*v[0]+.7152f*v[1]+.0722f*v[2]; }
static inline void oma_match_defaults(oma_match_params *p) {
  memset(p,0,sizeof(*p));
  p->exposure_on=p->wb_on=p->tone_strength=p->colour_strength=p->grain_strength=1;
  p->colour_scale=1; p->grain_size=1; p->grain_roughness=.5f;
  p->grain_shadows=p->grain_midtones=p->grain_highlights=1; p->protect_skin=1;
  p->grain_texture=1; p->render_version=5;
  p->tone_on=p->colour_on=p->grain_on=1;
}
static inline float oma_match_smooth(float a,float b,float x) {
  const float t=oma_match_clamp((x-a)/(b-a),0,1);
  return t*t*(3-2*t);
}
static inline int oma_match_identity(const oma_match_params *p) {
  if(p->exposure*p->exposure_on!=0 || p->warmth*p->wb_on!=0 || p->tint*p->wb_on!=0 || p->grain_amount*p->grain_strength*p->grain_on!=0) return 0;
  for(int i=0;i<9;++i) if(p->tone[i]*p->tone_strength*p->tone_on!=0) return 0;
  if(p->colour_strength*p->colour_on!=0) {
    if(p->colour_scale!=1) return 0;
    for(int i=0;i<3;++i) if(p->colour_a[i]!=0 || p->colour_b[i]!=0) return 0;
  }
  return 1;
}
static inline void oma_match_oklab(const float *v, float *o) {
  const float l=cbrtf(.4122214708f*v[0]+.5363325363f*v[1]+.0514459929f*v[2]);
  const float m=cbrtf(.2119034982f*v[0]+.6806995451f*v[1]+.1073969566f*v[2]);
  const float s=cbrtf(.0883024619f*v[0]+.2817188376f*v[1]+.6299787005f*v[2]);
  o[0]=.2104542553f*l+.7936177850f*m-.0040720468f*s;
  o[1]=1.9779984951f*l-2.4285922050f*m+.4505937099f*s;
  o[2]=.0259040371f*l+.7827717662f*m-.8086757660f*s;
}
static inline void oma_match_rgb(const float *o, float *v) {
  float l=o[0]+.3963377774f*o[1]+.2158037573f*o[2];
  float m=o[0]-.1055613458f*o[1]-.0638541728f*o[2];
  float s=o[0]-.0894841775f*o[1]-1.2914855480f*o[2];
  l=l*l*l; m=m*m*m; s=s*s*s;
  v[0]=4.0767416621f*l-3.3077115913f*m+.2309699292f*s;
  v[1]=-1.2684380046f*l+2.6097574011f*m-.3413193965f*s;
  v[2]=-.0041960863f*l-.7034186147f*m+1.7076147010f*s;
}
static inline void oma_match_weights(float y, float *w) {
  const float t=oma_match_clamp((y-.15f)/.7f,0,1)*2;
  w[0]=fmaxf(0,1-t); w[2]=fmaxf(0,t-1); w[1]=1-w[0]-w[2];
}
static inline void oma_match_normalise(const oma_match_params *p, const float *in, float *out) {
  const float warm=p->wb_on*p->warmth*.5f, tint=p->wb_on*p->tint*.5f;
  const float r=exp2f(warm+tint*.5f),g=exp2f(-tint),b=exp2f(-warm+tint*.5f);
  const float e=exp2f(p->exposure_on*p->exposure)/(.2126f*r+.7152f*g+.0722f*b);
  out[0]=in[0]*e*r; out[1]=in[1]*e*g; out[2]=in[2]*e*b;
}
static inline float oma_match_tone(const oma_match_params *p, float y) {
  // Extend with slope one beyond the SDR domain: do not crush highlight
  // headroom just because the reference is a JPEG.
  const float t=oma_match_clamp(y,0,1)*8;
  const int i=(int)fminf(t,7); const float f=t-i;
  return y+p->tone_on*p->tone_strength*((1-f)*p->tone[i]+f*p->tone[i+1]);
}
static inline void oma_match_colour(const oma_match_params *p, const float *in, float *out) {
  if(oma_match_identity(p)) {memcpy(out,in,3*sizeof(float));return;}
  oma_match_normalise(p,in,out);
  float y=oma_match_luma(out), encoded=oma_match_encode(fmaxf(y,0));
  const float ny=oma_match_decode(fmaxf(0,oma_match_tone(p,encoded)));
  if(y>1e-7f) for(int c=0;c<3;++c) out[c]*=ny/y;
  else if(ny>0) for(int c=0;c<3;++c) out[c]+=ny-y;
  if(p->colour_strength*p->colour_on==0) return;
  int identity=p->colour_scale==1;
  for(int z=0;z<3;++z) identity=identity && p->colour_a[z]==0 && p->colour_b[z]==0;
  if(identity) return;
  float lab[3],original[3]; oma_match_oklab(out,lab); memcpy(original,lab,sizeof(lab));
  float w[3]; oma_match_weights(encoded,w);
  float da=0,db=0; for(int z=0;z<3;++z) { da+=w[z]*p->colour_a[z]; db+=w[z]*p->colour_b[z]; }
  const float s=p->colour_strength*p->colour_on, scale=1+s*(p->colour_scale-1);
  lab[1]=lab[1]*scale+s*da; lab[2]=lab[2]*scale+s*db;
  // A soft chromaticity guard (not face detection). Avoid turning ordinary
  // skin-like colours orange/green when a reference has unrelated scenery.
  const float hue=atan2f(original[2],original[1]), chroma=hypotf(original[1],original[2]);
  float keep=oma_match_clamp(p->protect_skin,0,1);
  float protectedChroma=chroma;
  if(p->render_version>=5) {
    // Select from the incoming photograph: a look's WB/contrast must not
    // move neighbouring skin pixels into and out of protection. Relative
    // chroma is invariant to exposure, including darker and pinkish skin.
    float source[3];oma_match_oklab(in,source);
    const float sourceChroma=hypotf(source[1],source[2]);
    const float relative=sourceChroma/fmaxf(.01f,source[0]);
    const float sourceHue=atan2f(source[2],source[1]);
    keep*=oma_match_smooth(-.8f,-.2f,sourceHue)
         *(1-oma_match_smooth(1.2f,1.8f,sourceHue))
         *oma_match_smooth(.005f,.025f,relative)
         *(1-oma_match_smooth(.45f,.6f,relative))
         *oma_match_smooth(.02f,.12f,source[0])
         *oma_match_smooth(.35f,.65f,scale);
    // Bound the whole look's chroma relative to the original skin, including
    // WB. Scale C with L so changing tone/exposure cannot change saturation.
    // Fade in only for a substantive palette change. A tiny colour-profile
    // roundoff residual must not suddenly constrain an otherwise valid WB.
    const float anchor=sourceChroma*fmaxf(0,original[0])/fmaxf(.01f,source[0]);
    const float palette=oma_match_smooth(0,.02f,hypotf(s*da,s*db)+chroma*fabsf(scale-1));
    protectedChroma=chroma+s*palette*(anchor-chroma);
  } else if(p->render_version>=3) {
    // A hard chroma/hue selection creates false edges on smooth surfaces,
    // especially under a strong cyan look. Feather every eligibility boundary.
    keep*=oma_match_smooth(.2f,.5f,hue)
         *(1-oma_match_smooth(1.1f,1.5f,hue))
         *oma_match_smooth(.01f,.04f,chroma)
         *(1-oma_match_smooth(.15f,.23f,chroma))
         *oma_match_smooth(.1f,.3f,original[0])
         *oma_match_smooth(.35f,.65f,scale);
  } else if(!(scale>=.5f && hue>.35f && hue<1.3f && chroma>.025f && chroma<.19f && original[0]>.2f)) keep=0;
  if(keep>0) {
    const float difference=atan2f(lab[2],lab[1])-hue;
    // The sine also stays continuous across the opposite-hue wraparound.
    const float dh=p->render_version>=3?.10f*tanhf(sinf(difference)/.10f):oma_match_clamp(difference,-.10f,.10f);
    const float ca=cosf(hue+dh),cb=sinf(hue+dh);
    // Project onto the protected hue instead of rotating the candidate's
    // entire chroma back into skin. A strong green/cyan cast can have large
    // chroma in the opposite direction: rotating that magnitude made skin
    // redder (up to 25%) even when the requested saturation was below one.
    // Projection is the nearest colour on this hue ray; allow it to approach
    // neutral and respect desaturation. Keep older saved renders unchanged.
    const float nc=p->render_version>=5
      ? oma_match_clamp(lab[1]*ca+lab[2]*cb,protectedChroma*fminf(scale,1.25f)*.65f,protectedChroma*fminf(scale,1.25f))
      : p->render_version>=4
      ? oma_match_clamp(lab[1]*ca+lab[2]*cb,0,chroma*fminf(scale,1.25f))
      : oma_match_clamp(hypotf(lab[1],lab[2]),chroma*.75f,chroma*1.25f);
    lab[1]=lab[1]*(1-keep)+nc*ca*keep;
    lab[2]=lab[2]*(1-keep)+nc*cb*keep;
  }
  float candidate[3]; oma_match_rgb(lab,candidate);
  // Compress added chroma towards the same lightness before it leaves the
  // display gamut; preserve pre-existing out-of-range lightness/headroom.
  if(original[0]>=0 && original[0]<=1) {
    const float grey=lab[0]*lab[0]*lab[0]; float f=1;
    for(int c=0;c<3;++c) {
      if(candidate[c]<0) f=fminf(f,grey/(grey-candidate[c]+1e-10f));
      if(candidate[c]>1) f=fminf(f,(1-grey)/(candidate[c]-grey+1e-10f));
    }
    for(int c=0;c<3;++c) candidate[c]=grey+f*(candidate[c]-grey);
  }
  memcpy(out,candidate,3*sizeof(float));
}
static inline uint32_t oma_match_hash(int x,int y,uint32_t seed) {
  uint32_t h=(uint32_t)x*0x9e3779b9u ^ (uint32_t)y*0x85ebca6bu ^ seed;
  h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu; return h^(h>>16);
}
static inline float oma_match_random(int x,int y,uint32_t seed,float rough) {
  const uint32_t h=oma_match_hash(x,y,seed);
  // Variance-one triangular noise, with a bounded heavier-tailed component.
  const float a=((h&65535u)/65535.f-.5f)*2.449489743f;
  const float b=((h>>16)/65535.f-.5f)*2.449489743f;
  const float n=a+b, tail=n*fabsf(n)*.58f;
  return (1-rough)*n+rough*tail;
}
static inline float oma_match_noise(float x,float y,float size,uint32_t seed,float rough) {
  x/=size; y/=size; const int ix=(int)floorf(x),iy=(int)floorf(y);
  float u=x-ix,v=y-iy; u=u*u*(3-2*u); v=v*v*(3-2*v);
  const float a=oma_match_random(ix,iy,seed,rough),b=oma_match_random(ix+1,iy,seed,rough);
  const float c=oma_match_random(ix,iy+1,seed,rough),d=oma_match_random(ix+1,iy+1,seed,rough);
  const float norm=sqrtf(((1-u)*(1-u)+u*u)*((1-v)*(1-v)+v*v));
  return ((1-v)*((1-u)*a+u*b)+v*((1-u)*c+u*d))/fmaxf(.1f,norm);
}
static inline float oma_match_organic_noise(float x,float y,float size,uint32_t seed,float rough) {
  // Independent rotated fields remove the common horizontal/vertical lattice.
  // A finer octave retains small particles instead of enlarged square blobs.
  // Fixed world coordinates keep tiles/crops and equal-size outputs identical.
  const float a=oma_match_noise(.819152f*x+.573576f*y,-.573576f*x+.819152f*y,size,seed,rough);
  const float b=oma_match_noise(.374607f*x-.927184f*y,.927184f*x+.374607f*y,size*.91f,seed^0xb7e15162u,rough);
  const float c=oma_match_noise(.956305f*x-.292372f*y,.292372f*x+.956305f*y,size*.43f,seed^0x8aed2a6bu,rough);
  const float fine=.3f+.3f*rough;
  return (a+b+fine*c)/sqrtf(2+fine*fine);
}
static inline void oma_match_grain(const oma_match_params *p,float *rgb,float x,float y,float pixelScale,float fullEdge) {
  if(p->grain_amount<=0 || p->grain_strength<=0 || p->grain_on<=0) return;
  float encoded[3]; for(int c=0;c<3;++c) encoded[c]=oma_match_encode(rgb[c]);
  const float lum=oma_match_luma(encoded); float w[3]; oma_match_weights(lum,w);
  const float weight=w[0]*p->grain_shadows+w[1]*p->grain_midtones+w[2]*p->grain_highlights;
  const float radius=fmaxf(.35f,p->grain_size*fullEdge/3000.f);
  // Minification removes unresolvable grain energy. Evaluate a stable
  // coarser octave instead of aliasing native noise into a fitted preview.
  const float footprint=1/fmaxf(pixelScale,1e-5f), size=fmaxf(radius,footprint);
  const float attenuation=fminf(1,fmaxf(1,radius)/footprint);
  x-=.5f*footprint; y-=.5f*footprint;
  const float amount=p->grain_amount/255.f*p->grain_strength*p->grain_on*weight*attenuation;
  const float mix=oma_match_clamp(p->grain_colour,0,1);
  const int organic=p->grain_texture>=.5f;
  const float mono=organic?oma_match_organic_noise(x,y,size,0x243f6a88u,p->grain_roughness):oma_match_noise(x,y,size,0x243f6a88u,p->grain_roughness);
  const float norm=sqrtf((1-mix)*(1-mix)+mix*mix);
  for(int c=0;c<3;++c) {
    const float coloured=organic?oma_match_organic_noise(x,y,size,0x85a308d3u+101u*c,p->grain_roughness):oma_match_noise(x,y,size,0x85a308d3u+101u*c,p->grain_roughness);
    const float noise=((1-mix)*mono+mix*coloured)/norm;
    // Smoothly taper new noise close to black/white, preserving existing
    // headroom without adding clipped pixels. Bound the heavy-tailed noise.
    float delta=amount*noise;
    if(p->render_version>=3 || (encoded[c]>=0 && encoded[c]<=1)) {
      // Matrix/gamut arithmetic can leave a channel a few ulps below zero.
      // Do not turn that rounding residue into full-strength negative noise.
      // Existing out-of-range headroom is retained, without added grain.
      const float room=fmaxf(0,fminf(encoded[c],1-encoded[c]));
      delta=room>0?room*tanhf(delta/room):0;
    }
    const float v=encoded[c]+delta;
    rgb[c]=oma_match_decode(v);
  }
}
