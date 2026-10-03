// SPDX-License-Identifier: GPL-3.0-or-later
// A separate, editable match over the existing develop result. No LUT files,
// source rewrites or changes to another tool's parameters are involved.
#include "common/iop_profile.h"
#include "common/imagematchmath.h"
#include "develop/imageop.h"
#include "iop/iop_api.h"

DT_MODULE_INTROSPECTION(4, dt_iop_omarawmatch_params_t)
typedef struct dt_iop_omarawmatch_params_t {
  float exposure; // $MIN: -3.0 $MAX: 3.0 $DEFAULT: 0.0
  float warmth; // $MIN: -1.0 $MAX: 1.0 $DEFAULT: 0.0
  float tint; // $MIN: -1.0 $MAX: 1.0 $DEFAULT: 0.0
  float exposure_on; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float wb_on; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float tone_strength; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float colour_strength; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float grain_strength; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float tone[9]; // $MIN: -0.4 $MAX: 0.4 $DEFAULT: 0.0
  float colour_a[3]; // $MIN: -0.1 $MAX: 0.1 $DEFAULT: 0.0
  float colour_b[3]; // $MIN: -0.1 $MAX: 0.1 $DEFAULT: 0.0
  float colour_scale; // $MIN: 0.0 $MAX: 2.0 $DEFAULT: 1.0
  float grain_amount; // $MIN: 0.0 $MAX: 32.0 $DEFAULT: 0.0
  float grain_size; // $MIN: 0.2 $MAX: 12.0 $DEFAULT: 1.0
  float grain_roughness; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 0.5
  float grain_colour; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 0.0
  float grain_shadows; // $MIN: 0.0 $MAX: 2.0 $DEFAULT: 1.0
  float grain_midtones; // $MIN: 0.0 $MAX: 2.0 $DEFAULT: 1.0
  float grain_highlights; // $MIN: 0.0 $MAX: 2.0 $DEFAULT: 1.0
  float protect_skin; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float grain_texture; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float render_version; // $MIN: 1.0 $MAX: 5.0 $DEFAULT: 5.0
  float tone_on; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float colour_on; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
  float grain_on; // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 1.0
} dt_iop_omarawmatch_params_t;
_Static_assert(sizeof(dt_iop_omarawmatch_params_t)==sizeof(oma_match_params),"match history layout");
int legacy_params(dt_iop_module_t *self, const void *const old_params,
                  const int old_version, void **new_params,
                  int32_t *new_params_size, int *new_version) {
  if(old_version<1 || old_version>3) return 1;
  oma_match_params *p=calloc(1,sizeof(*p));
  memcpy(p,old_params,(old_version==1?32:old_version==2?33:34)*sizeof(float));
  // Retain the exact saved appearance until the user makes a fresh match.
  if(old_version<3)p->render_version=old_version;
  p->tone_on=p->colour_on=p->grain_on=1;
  *new_params=p;*new_params_size=sizeof(*p);*new_version=4;
  return 0;
}
const char *name() { return _("Image Match"); }
int default_group() { return IOP_GROUP_COLOR | IOP_GROUP_GRADING; }
int flags() { return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_ONE_INSTANCE | IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_HIDDEN; }
dt_iop_colorspace_type_t default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece) { return IOP_CS_RGB; }
void commit_params(dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece) {
  memcpy(piece->data,params,sizeof(oma_match_params));
}
void process(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const input,
             void *const output, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out) {
  const oma_match_params *p=piece->data;
  const float *in=input; float *out=output;
  if(oma_match_identity(p)) {memcpy(out,in,(size_t)roi_out->width*roi_out->height*4*sizeof(float));return;}
  const dt_iop_order_iccprofile_info_t *work=dt_ioppr_get_pipe_current_profile_info(self,piece->pipe);
  const dt_iop_order_iccprofile_info_t *target=dt_ioppr_add_profile_info_to_list(self->dev,DT_COLORSPACE_LIN_REC709,"",INTENT_PERCEPTUAL);
  if(!work || !target || !dt_is_valid_colormatrix(work->matrix_in[0][0])) {
    memcpy(out,in,(size_t)roi_out->width*roi_out->height*4*sizeof(float)); return;
  }
  const float scale=roi_out->scale/piece->iscale;
  const float edge=fmaxf(piece->buf_in.width,piece->buf_in.height)/piece->iscale;
  DT_OMP_FOR()
  for(int y=0;y<roi_out->height;++y) for(int x=0;x<roi_out->width;++x) {
    const size_t k=(size_t)y*roi_out->width+x;
    dt_aligned_pixel_t pixel={in[4*k],in[4*k+1],in[4*k+2],in[4*k+3]},xyz={0},linear={0},matched={0},result={0};
    if(!isfinite(pixel[0]) || !isfinite(pixel[1]) || !isfinite(pixel[2])) { memcpy(out+4*k,in+4*k,4*sizeof(float)); continue; }
    dt_ioppr_rgb_matrix_to_xyz(pixel,xyz,work->matrix_in_transposed,work->lut_in,work->unbounded_coeffs_in,work->lutsize,work->nonlinearlut);
    dt_apply_transposed_color_matrix(xyz,target->matrix_out_transposed,linear);
    oma_match_colour(p,linear,matched);
    oma_match_grain(p,matched,(roi_out->x+x+.5f)/scale,(roi_out->y+y+.5f)/scale,scale,edge);
    dt_apply_transposed_color_matrix(matched,target->matrix_in_transposed,xyz);
    dt_ioppr_xyz_to_rgb_matrix(xyz,result,work->matrix_out_transposed,work->lut_out,work->unbounded_coeffs_out,work->lutsize,work->nonlinearlut);
    for(int c=0;c<3;++c) out[4*k+c]=result[c];
    out[4*k+3]=in[4*k+3];
  }
}
void init_pipe(dt_iop_module_t *self,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece) { piece->data=calloc(1,sizeof(oma_match_params)); }
void cleanup_pipe(dt_iop_module_t *self,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece) { free(piece->data); piece->data=NULL; }
void init(dt_iop_module_t *self) {
  self->params_size=sizeof(dt_iop_omarawmatch_params_t);
  self->params=calloc(1,self->params_size); self->default_params=calloc(1,self->params_size);
  oma_match_defaults((oma_match_params *)self->default_params); memcpy(self->params,self->default_params,self->params_size);
  self->default_enabled=FALSE; self->hide_enable_button=FALSE;
}
void gui_init(dt_iop_module_t *self) { }
void gui_update(dt_iop_module_t *self) { }
void gui_cleanup(dt_iop_module_t *self) { }
