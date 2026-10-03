// SPDX-License-Identifier: GPL-3.0-or-later
// Independent creative profile, after the print and before output conversion.
#include "common/iop_profile.h"
#include "common/image.h"
#include "common/creativeprofiledata.h"
#include "develop/imageop.h"
#include "iop/iop_api.h"

DT_MODULE_INTROSPECTION(1, dt_iop_omarawprofile_params_t)
typedef enum oma_profile_look_t {
  OMA_PROFILE_NONE = 0, // $DESCRIPTION: "None"
  OMA_PROFILE_PORTRAIT = 1, // $DESCRIPTION: "Warm Portrait"
  OMA_PROFILE_LANDSCAPE = 2, // $DESCRIPTION: "Clear Landscape"
  OMA_PROFILE_SOFT = 3, // $DESCRIPTION: "Soft Colour"
  OMA_PROFILE_CINEMA = 4, // $DESCRIPTION: "Cinema Dusk"
  OMA_PROFILE_SILVER = 5, // $DESCRIPTION: "Silver Monochrome"
  OMA_PROFILE_WARM_BW = 6, // $DESCRIPTION: "Warm Monochrome"
  OMA_PROFILE_CUSTOM = 7, // $DESCRIPTION: "Imported LUT"
} oma_profile_look_t;
typedef enum oma_profile_space_t {
  OMA_PROFILE_SRGB = 0, // $DESCRIPTION: "sRGB"
  OMA_PROFILE_RGB_1998 = 1, // $DESCRIPTION: "RGB (1998)"
  OMA_PROFILE_LINEAR_709 = 2, // $DESCRIPTION: "Linear Rec. 709"
  OMA_PROFILE_LINEAR_2020 = 3, // $DESCRIPTION: "Linear Rec. 2020"
} oma_profile_space_t;
typedef struct dt_iop_omarawprofile_params_t {
  oma_profile_look_t look; // $DEFAULT: OMA_PROFILE_NONE
  float amount; // $MIN: 0.0 $MAX: 200.0 $DEFAULT: 100.0
  char filepath[512];
  oma_profile_space_t colorspace; // $DEFAULT: OMA_PROFILE_SRGB
} dt_iop_omarawprofile_params_t;
typedef struct dt_iop_omarawprofile_data_t {
  dt_iop_omarawprofile_params_t params;
  oma_creative_data profile;
} dt_iop_omarawprofile_data_t;
const char *name() {
#ifdef OMA_PROFILE_EARLY
  return _("creative profile look table");
#else
  return _("creative profile");
#endif
}
int default_group() { return IOP_GROUP_COLOR | IOP_GROUP_GRADING; }
int flags() {
#ifdef OMA_PROFILE_EARLY
  return IOP_FLAGS_ONE_INSTANCE | IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_HIDDEN;
#else
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_HIDDEN;
#endif
}
dt_iop_colorspace_type_t default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece) { return IOP_CS_RGB; }
void commit_params(dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_omarawprofile_data_t *d=piece->data;
  const dt_iop_omarawprofile_params_t *p=(const dt_iop_omarawprofile_params_t *)params;
  if(strncmp(d->params.filepath,p->filepath,sizeof(p->filepath)) || d->params.look != p->look)
  {
    oma_creative_clear(&d->profile);
    if(p->look == OMA_PROFILE_CUSTOM && memchr(p->filepath,0,sizeof(p->filepath)) && *p->filepath)
      if(!oma_creative_read(p->filepath,&d->profile)) dt_print(DT_DEBUG_ALWAYS,"[omarawprofile] missing or invalid LUT: %s",p->filepath);
  }
  d->params=*p;
  if(d->profile.enhanced) {
    const int scene=dt_image_is_rawprepare_supported(&self->dev->image_storage);
    if(!(scene ? d->profile.scene : d->profile.output)) piece->enabled=FALSE;
  }
  d->params.amount=isfinite(p->amount) ? oma_profile_clamp(p->amount,0,200) : 0;
}
void process(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const input,
             void *const output, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const dt_iop_omarawprofile_data_t *d=piece->data;
  const float *in=input; float *out=output;
  const size_t pixels=(size_t)roi_out->width*roi_out->height;
  const int look=d->params.look; const float amount=d->params.amount/100.f;
  const oma_creative_data *profile=&d->profile;
#ifdef OMA_PROFILE_EARLY
  const int early=1;
#else
  const int early=0;
#endif
  if(early || (look==7 && profile->enhanced)) {
    const int present=early ? profile->hsv.data!=NULL : profile->cube.rgb!=NULL;
    const float strength=early
      ? oma_creative_amount(d->params.amount,profile->hsv.minimum,profile->hsv_amount,profile->hsv.maximum,profile->supports_amount)
      : oma_creative_amount(d->params.amount,profile->minimum,profile->rgb_amount,profile->maximum,profile->supports_amount);
    if(look!=7 || !profile->enhanced || !present || strength==0) { memcpy(out,in,pixels*4*sizeof(float)); return; }
    const int primaries[]={DT_COLORSPACE_LIN_REC709,DT_COLORSPACE_ADOBERGB,DT_COLORSPACE_PROPHOTO_RGB,
                           DT_COLORSPACE_DISPLAY_P3,DT_COLORSPACE_LIN_REC2020};
    const dt_iop_order_iccprofile_info_t *work=dt_ioppr_get_pipe_current_profile_info(self,piece->pipe);
    const dt_iop_order_iccprofile_info_t *target=dt_ioppr_add_profile_info_to_list(self->dev,
              early ? DT_COLORSPACE_PROPHOTO_RGB : primaries[profile->primaries],"",INTENT_PERCEPTUAL);
    if(!work || !target || !dt_is_valid_colormatrix(work->matrix_in[0][0])) { memcpy(out,in,pixels*4*sizeof(float)); return; }
    DT_OMP_FOR()
    for(size_t k=0;k<pixels;++k) {
      dt_aligned_pixel_t inputRGB={in[4*k],in[4*k+1],in[4*k+2],in[4*k+3]},xyz={0},linear={0},styled={0},result={0};
      if(!isfinite(inputRGB[0]) || !isfinite(inputRGB[1]) || !isfinite(inputRGB[2])) { memcpy(out+4*k,in+4*k,4*sizeof(float)); continue; }
      dt_ioppr_rgb_matrix_to_xyz(inputRGB,xyz,work->matrix_in_transposed,work->lut_in,
                                 work->unbounded_coeffs_in,work->lutsize,work->nonlinearlut);
      dt_apply_transposed_color_matrix(xyz,target->matrix_out_transposed,linear);
      if(early) oma_creative_hsv_pixel(&profile->hsv,linear,styled,strength);
      else oma_creative_rgb_pixel(profile,linear,styled,strength);
      dt_apply_transposed_color_matrix(styled,target->matrix_in_transposed,xyz);
      dt_ioppr_xyz_to_rgb_matrix(xyz,result,work->matrix_out_transposed,work->lut_out,
                                 work->unbounded_coeffs_out,work->lutsize,work->nonlinearlut);
      for(int c=0;c<3;++c) out[4*k+c]=result[c];
      out[4*k+3]=in[4*k+3];
    }
    return;
  }
  if(!amount || look<=0 || look>7 || (look==7 && !d->profile.cube.rgb)) { memcpy(out,in,pixels*4*sizeof(float)); return; }
  const int spaces[]={DT_COLORSPACE_SRGB,DT_COLORSPACE_ADOBERGB,DT_COLORSPACE_LIN_REC709,DT_COLORSPACE_LIN_REC2020};
  const int selected=look==7 && d->params.colorspace>=0 && d->params.colorspace<4 ? d->params.colorspace : 0;
  const dt_iop_order_iccprofile_info_t *work=dt_ioppr_get_pipe_current_profile_info(self,piece->pipe);
  const dt_iop_order_iccprofile_info_t *space=dt_ioppr_add_profile_info_to_list(self->dev,spaces[selected],"",INTENT_PERCEPTUAL);
  if(!work || !space) { memcpy(out,in,pixels*4*sizeof(float)); return; }
  dt_ioppr_transform_image_colorspace_rgb(in,out,roi_out->width,roi_out->height,work,space,"creative profile input");
  DT_OMP_FOR()
  for(size_t k=0;k<pixels;++k)
  {
    float *v=out+4*k, styled[3];
    if(!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2])) continue;
    if(look==7) oma_cube_pixel(&d->profile.cube,v,styled); else oma_profile_builtin(look,v,styled);
    // Once fully monochrome, increasing Amount strengthens tone/colouring
    // without bringing back inverted chroma from the original photograph.
    const float grey=.2126f*v[0]+.7152f*v[1]+.0722f*v[2];
    for(int c=0;c<3;++c) {
      const float base=(look==5 || look==6) && amount>1 ? grey : v[c];
      v[c]=base+amount*(styled[c]-base);
    }
    v[3]=in[4*k+3];
  }
  dt_ioppr_transform_image_colorspace_rgb(out,out,roi_out->width,roi_out->height,space,work,"creative profile output");
}
void init_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{ piece->data=calloc(1,sizeof(dt_iop_omarawprofile_data_t)); }
void cleanup_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{ oma_creative_clear(&((dt_iop_omarawprofile_data_t *)piece->data)->profile); free(piece->data); piece->data=NULL; }
