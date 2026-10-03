// SPDX-License-Identifier: GPL-3.0-or-later
// Independent DNG profile gain correction.
// This product includes DNG technology under license by Adobe.
#include "common/image.h"
#include "common/iop_profile.h"
#include "common/omaraw_dng_gain.h"
#include "develop/imageop.h"
#include "iop/iop_api.h"

DT_MODULE_INTROSPECTION(1, dt_iop_omarawdnggain_params_t)
typedef struct dt_iop_omarawdnggain_params_t { int version; } dt_iop_omarawdnggain_params_t;
typedef struct dt_iop_omarawdnggain_data_t { oma_dng_gain map; dt_imgid_t image; } dt_iop_omarawdnggain_data_t;
const char *name() { return _("DNG profile gain map"); }
int default_group() { return IOP_GROUP_TECHNICAL; }
int flags() { return IOP_FLAGS_ONE_INSTANCE | IOP_FLAGS_HIDDEN | IOP_FLAGS_ALLOW_TILING; }
dt_iop_colorspace_type_t default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece) { return IOP_CS_RGB; }
static int read_map(dt_iop_module_t *self,oma_dng_gain *map)
{
  if(!self->dev || self->dev->image_storage.id<0) return 0;
  char path[PATH_MAX]; gboolean from_cache=FALSE;
  dt_image_full_path(self->dev->image_storage.id,path,sizeof(path),&from_cache);
  return oma_dng_gain_read(path,map);
}
void reload_defaults(dt_iop_module_t *self)
{
  oma_dng_gain map={0}; const int status=read_map(self,&map);
  oma_dng_gain_clear(&map);
  self->default_enabled=status>0;
  self->hide_enable_button=TRUE;
  if(status<0) dt_print(DT_DEBUG_ALWAYS,"[omarawdnggain] invalid DNG profile gain metadata");
}
void commit_params(dt_iop_module_t *self,dt_iop_params_t *params,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_omarawdnggain_data_t *d=piece->data;
  if(d->image!=self->dev->image_storage.id) {
    oma_dng_gain_clear(&d->map); read_map(self,&d->map); d->image=self->dev->image_storage.id;
  }
  if(!d->map.width) piece->enabled=FALSE;
}
void process(dt_iop_module_t *self,dt_dev_pixelpipe_iop_t *piece,const void *const input,
             void *const output,const dt_iop_roi_t *const roi_in,const dt_iop_roi_t *const roi_out)
{
  const oma_dng_gain *m=&((dt_iop_omarawdnggain_data_t *)piece->data)->map;
  const float *in=input; float *out=output;
  const size_t pixels=(size_t)roi_out->width*roi_out->height;
  if(!m->values) {
    DT_OMP_FOR()
    for(size_t i=0;i<pixels;++i) {
      for(int c=0;c<3;++c) out[4*i+c]=in[4*i+c]*m->baseline;
      out[4*i+3]=in[4*i+3];
    }
    return;
  }
  const dt_iop_order_iccprofile_info_t *work=dt_ioppr_get_pipe_current_profile_info(self,piece->pipe);
  const dt_iop_order_iccprofile_info_t *rimm=dt_ioppr_add_profile_info_to_list(self->dev,DT_COLORSPACE_PROPHOTO_RGB,"",INTENT_PERCEPTUAL);
  if(!m->values || !work || !rimm) { memcpy(out,in,pixels*4*sizeof(float)); return; }
  // RIMM is linear ProPhoto RGB (D50). Lookup follows BaselineExposure.
  dt_ioppr_transform_image_colorspace_rgb(in,out,roi_out->width,roi_out->height,work,rimm,"DNG profile gain input");
  // Undo the user's crop/rotation/lens geometry to keep the gain map anchored
  // to the original active area at every preview/export size. Bounded strips
  // avoid another full-resolution allocation just for coordinates.
  float *points=dt_alloc_align_float((size_t)roi_out->width*64*2);
  if(!points) { memcpy(out,in,pixels*4*sizeof(float)); return; }
  for(int row=0;row<roi_out->height;row+=64) {
    const int rows=MIN(64,roi_out->height-row), count=rows*roi_out->width;
    for(int y=0;y<rows;++y) for(int x=0;x<roi_out->width;++x) {
      const size_t p=(size_t)y*roi_out->width+x;
      points[2*p]=(roi_out->x+x+.5f)/roi_out->scale;
      points[2*p+1]=(roi_out->y+row+y+.5f)/roi_out->scale;
    }
    const gboolean valid=dt_dev_distort_backtransform_plus(self->dev,piece->pipe,self->iop_order,
                                                          DT_DEV_TRANSFORM_DIR_BACK_EXCL,points,count);
    DT_OMP_FOR()
    for(int p=0;p<count;++p) {
      const size_t index=(size_t)row*roi_out->width+p;
      float *v=out+4*index;
      if(!valid || !isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2])) continue;
      for(int c=0;c<3;++c) v[c]*=m->baseline;
      const double x=(points[2*p]*m->width/piece->pipe->iwidth-m->active[1])/(m->active[3]-m->active[1]);
      const double y=(points[2*p+1]*m->height/piece->pipe->iheight-m->active[0])/(m->active[2]-m->active[0]);
      const float gain=oma_dng_gain_at(m,x,y,v);
      for(int c=0;c<3;++c) v[c]*=gain;
      v[3]=in[4*index+3];
    }
  }
  dt_free_align(points);
  dt_ioppr_transform_image_colorspace_rgb(out,out,roi_out->width,roi_out->height,rimm,work,"DNG profile gain output");
}
void init_pipe(dt_iop_module_t *self,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece)
{ piece->data=calloc(1,sizeof(dt_iop_omarawdnggain_data_t)); ((dt_iop_omarawdnggain_data_t *)piece->data)->image=-1; }
void cleanup_pipe(dt_iop_module_t *self,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece)
{ oma_dng_gain_clear(&((dt_iop_omarawdnggain_data_t *)piece->data)->map); free(piece->data); piece->data=NULL; }
