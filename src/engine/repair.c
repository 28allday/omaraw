// SPDX-License-Identifier: GPL-3.0-or-later
// Immutable, sparse AI repairs. Coordinates and RGB belong to this early
// pipeline stage; all downstream grading and geometry process them normally.
#include "common/iop_profile.h"
#include "control/conf.h"
#include "develop/imageop.h"
#include "iop/iop_api.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DT_MODULE_INTROSPECTION(1, dt_iop_omarawrepair_params_t)
typedef struct dt_iop_omarawrepair_params_t { char file[65]; } dt_iop_omarawrepair_params_t;
typedef struct { char magic[8]; uint32_t width,height,x,y,w,h; char parent[64]; } repair_header;
typedef struct { char magic[8]; uint32_t width,height; char parent[64]; float matrix[9]; } source_header;
typedef struct { GMappedFile *map; const repair_header *head; const float *pixels; } repair_patch;
typedef struct {
  GArray *patches;
  char error[256], file[65], capture[4096];
  int captured;
} repair_data;

const char *name() { return _("AI object removal"); }
int default_group() { return IOP_GROUP_CORRECT; }
int flags() { return IOP_FLAGS_HIDDEN | IOP_FLAGS_ALLOW_TILING; }
dt_iop_colorspace_type_t default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe,
                                          dt_dev_pixelpipe_iop_t *piece) { return IOP_CS_RGB; }
static int key_valid(const char *key) {
  if(strlen(key)!=64) return 0;
  for(int i=0;i<64;i++) if(!((key[i]>='0'&&key[i]<='9')||(key[i]>='a'&&key[i]<='f'))) return 0;
  return 1;
}
static void clear(repair_data *d) {
  if(d->patches) { for(guint i=0;i<d->patches->len;i++) g_mapped_file_unref(g_array_index(d->patches,repair_patch,i).map); g_array_free(d->patches,TRUE); }
  d->patches=NULL;
}
static void read_chain(repair_data *d, const char *key) {
  clear(d); d->error[0]=0;
  d->patches=g_array_new(FALSE,FALSE,sizeof(repair_patch));
  char next[65]; g_strlcpy(next,key,sizeof next);
  char *folder=dt_conf_get_string("plugins/darkroom/omarawrepair/directory");
  while(next[0]) {
    if(!key_valid(next) || d->patches->len>=4096) { g_strlcpy(d->error,"Invalid AI repair history",sizeof d->error); break; }
    char *name=g_strconcat(next,".orp",NULL), *path=g_build_filename(folder,name,NULL); g_free(name);
    GMappedFile *map=g_mapped_file_new(path,FALSE,NULL); g_free(path);
    if(!map) { g_strlcpy(d->error,"Saved AI repair is missing; restore the catalogue's repairs folder",sizeof d->error); break; }
    const size_t bytes=g_mapped_file_get_length(map);
    const char *data=g_mapped_file_get_contents(map);
    repair_header h={0}; if(bytes>=sizeof h) memcpy(&h,data,sizeof h);
    char *sha=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar*)data,bytes);
    const int valid=bytes>=sizeof h && !memcmp(h.magic,"OREPAIR1",8) && !strcmp(sha,next)
      && h.width>0 && h.height>0 && (uint64_t)h.width*h.height<=110000000
      && h.w>0 && h.h>0 && h.x<h.width && h.y<h.height && h.w<=h.width-h.x && h.h<=h.height-h.y
      && bytes==sizeof h+(uint64_t)h.w*h.h*16;
    g_free(sha);
    if(!valid) { g_mapped_file_unref(map); g_strlcpy(d->error,"Saved AI repair is damaged",sizeof d->error); break; }
    const float *values=(const float*)(data+sizeof h);
    for(size_t i=0;valid && i<(size_t)h.w*h.h*4;i++) if(!isfinite(values[i]) || (i%4==3 && (values[i]<0.f || values[i]>1.f))) {g_strlcpy(d->error,"Saved AI repair contains invalid pixels",sizeof d->error);break;}
    if(d->error[0]) {g_mapped_file_unref(map);break;}
    repair_patch p={map,(const repair_header*)data,(const float*)(data+sizeof h)};
    g_array_append_val(d->patches,p);
    memcpy(next,h.parent,64); next[64]=0;
  }
  g_free(folder);
}
void init_pipe(dt_iop_module_t *self,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece) { piece->data=calloc(1,sizeof(repair_data)); }
void cleanup_pipe(dt_iop_module_t *self,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece) { clear(piece->data); free(piece->data); piece->data=NULL; }
void commit_params(dt_iop_module_t *self,dt_iop_params_t *params,dt_dev_pixelpipe_t *pipe,dt_dev_pixelpipe_iop_t *piece) {
  repair_data *d=piece->data; const dt_iop_omarawrepair_params_t *p=(const void*)params;
  if(d->patches && !strncmp(d->file,p->file,64)) return;
  memcpy(d->file,p->file,64); d->file[64]=0; read_chain(d,d->file);
}
// Used only by OmaRAW's serial worker on a private pixelpipe.
__attribute__((visibility("default"))) int omaraw_repair_capture(dt_dev_pixelpipe_iop_t *piece,const char *path) {
  repair_data *d=piece->data; d->captured=0; g_strlcpy(d->capture,path,sizeof d->capture); return !d->error[0];
}
__attribute__((visibility("default"))) const char *omaraw_repair_error(dt_dev_pixelpipe_iop_t *piece) { return ((repair_data*)piece->data)->error; }
static void capture(dt_iop_module_t *self,dt_dev_pixelpipe_iop_t *piece,const float *pixels,const dt_iop_roi_t *roi) {
  repair_data *d=piece->data;
  if(!d->capture[0]) return;
  source_header h={.magic="ORESRC01",.width=roi->width,.height=roi->height};
  memcpy(h.parent,d->file,64);
  const dt_iop_order_iccprofile_info_t *work=dt_ioppr_get_pipe_current_profile_info(self,piece->pipe);
  const dt_iop_order_iccprofile_info_t *rgb=dt_ioppr_add_profile_info_to_list(self->dev,DT_COLORSPACE_LIN_REC709,"",DT_INTENT_RELATIVE_COLORIMETRIC);
  if(roi->x || roi->y || roi->scale!=1.f || !work || !rgb || !isfinite(work->matrix_in[0][0])) {
    g_strlcpy(d->error,"Cannot prepare a linear full-resolution AI source",sizeof d->error); return;
  }
  for(int r=0;r<3;r++) for(int c=0;c<3;c++) for(int k=0;k<3;k++) h.matrix[r*3+c]+=rgb->matrix_out[r][k]*work->matrix_in[k][c];
  FILE *f=g_fopen(d->capture,"wb");
  if(!f) { g_strlcpy(d->error,"Cannot write AI source",sizeof d->error); return; }
  int ok=fwrite(&h,1,sizeof h,f)==sizeof h && fwrite(pixels,16,(size_t)h.width*h.height,f)==(size_t)h.width*h.height;
  if(fclose(f)) ok=0;
  if(!ok) g_strlcpy(d->error,"Cannot finish writing AI source",sizeof d->error);
  else d->captured=1;
}
void process(dt_iop_module_t *self,dt_dev_pixelpipe_iop_t *piece,const void *input,void *output,
             const dt_iop_roi_t *roi_in,const dt_iop_roi_t *roi_out) {
  repair_data *d=piece->data; float *out=output;
  memcpy(out,input,(size_t)roi_out->width*roi_out->height*16);
  if(d->error[0]) { piece->pipe->shutdown=TRUE; return; }
  // Oldest first. Overlap replaces only the new mask; disjoint repairs remain.
  for(int i=d->patches?(int)d->patches->len-1:-1;i>=0;i--) {
    const repair_patch *p=&g_array_index(d->patches,repair_patch,i); const repair_header *h=p->head;
    const double sx=(double)h->width/(piece->buf_in.width*roi_out->scale), sy=(double)h->height/(piece->buf_in.height*roi_out->scale);
    const int left=MAX(0,(int)floor(((double)h->x-1)/sx-roi_out->x)), right=MIN(roi_out->width,(int)ceil(((double)h->x+h->w+1)/sx-roi_out->x));
    const int top=MAX(0,(int)floor(((double)h->y-1)/sy-roi_out->y)), bottom=MIN(roi_out->height,(int)ceil(((double)h->y+h->h+1)/sy-roi_out->y));
    DT_OMP_FOR()
    for(int y=top;y<bottom;y++) for(int x=left;x<right;x++) {
      const double px=(roi_out->x+x+.5)*sx-.5-h->x, py=(roi_out->y+y+.5)*sy-.5-h->y;
      if(px<=-1 || py<=-1 || px>=h->w || py>=h->h) continue;
      const int ix=floor(px),iy=floor(py); const float fx=px-ix,fy=py-iy;
      float rgba[4]={0};
      for(int j=0;j<2;j++) for(int k=0;k<2;k++) {
        int xx=ix+k,yy=iy+j; if(xx<0||yy<0||xx>=h->w||yy>=h->h) continue;
        const float *v=p->pixels+((size_t)yy*h->w+xx)*4;
        float weight=(k?fx:1-fx)*(j?fy:1-fy)*v[3];
        for(int c=0;c<3;c++) rgba[c]+=v[c]*weight;
        rgba[3]+=weight;
      }
      if(rgba[3]<=0) continue;
      float *v=out+((size_t)y*roi_out->width+x)*4;
      for(int c=0;c<3;c++) v[c]=v[c]*(1-rgba[3])+rgba[c];
    }
  }
  capture(self,piece,out,roi_out);
}
