// Included by bridge.c: uses the same serial develop context and private pipe.
int oma_engine_retouch_preview(int imgid,int scale,int width,int height,float **out,int *w,int *h) {
    if(scale<0 || scale>16 || load(imgid))return -1;
    dt_iop_module_t *m=retouch_module();void (*preview)(int)=NULL;
    if(m && m->so->module)g_module_symbol(m->so->module,"omaraw_retouch_preview_v1",(gpointer *)&preview);
    if(!preview){set_error("Frequency preview needs the updated private engine",NULL);return -1;}
    // This diagnostic render is isolated from cached normal frames and exports.
    clear_preview_cache();preview(scale);
    const int rc=render_preview(imgid,0,0,MIN(width,1400),MIN(height,1400),out,w,h);
    preview(-1);clear_preview_cache();return rc;
}
int oma_engine_mask_coordinates(int imgid,float *xy,int count,int forward) {
    if(!xy || count<1 || count>4096 || load(imgid)) return -1;
    for(int i=0;i<count*2;++i)if(!isfinite(xy[i]) || fabsf(xy[i])>8)return -1;
    oma_preview_cache *cache=prepare_geometry_preview(imgid,0);if(!cache)return -1;
    const float width=cache->pipe.processed_width,height=cache->pipe.processed_height;
    const float iw=cache->dev.image_storage.width,ih=cache->dev.image_storage.height;
    if(iw<=0 || ih<=0 || width<=0 || height<=0)return -1;
    for(int i=0;i<count*2;++i)xy[i]*=forward?(i%2?ih:iw):(i%2?height:width);
    if(forward)dt_dev_distort_transform_plus(&cache->dev,&cache->pipe,0,DT_DEV_TRANSFORM_DIR_ALL,xy,count);
    else dt_dev_distort_backtransform_plus(&cache->dev,&cache->pipe,0,DT_DEV_TRANSFORM_DIR_ALL,xy,count);
    for(int i=0;i<count*2;++i) {xy[i]/=forward?(i%2?height:width):(i%2?ih:iw);if(!isfinite(xy[i]))return -1;}
    return 0;
}
int oma_engine_spot_points(int imgid,int index,float *xy,int capacity) {
    if(load(imgid))return -1;
    dt_iop_module_t *m=retouch_module();dt_masks_form_t *form=NULL;
    if(!m || !spot_entry(m,index,&form) || !form || !(form->type&(DT_MASKS_BRUSH|DT_MASKS_PATH)))return 0;
    int n=0;for(GList *l=form->points;l;l=l->next,++n)if(xy && n<capacity){const float *p=l->data;xy[n*2]=p[0];xy[n*2+1]=p[1];}
    return n;
}
int oma_engine_spot_set_scale(int imgid,int index,int scale) {
    if(scale<0 || scale>16 || load(imgid))return -1;
    dt_iop_module_t *m=retouch_module();dt_masks_form_t *form=NULL;
    if(!m || !spot_entry(m,index,&form) || !form)return -1;
    rt_form_data *f=rt_forms_of(m);const int i=rt_index_of(m,form->formid);if(!f || i<0)return -1;
    f[i].scale=scale;
    dt_dev_add_history_item_ext(&g_dev,m,TRUE,TRUE);dt_dev_write_history(&g_dev);return 0;
}
int oma_engine_spot_add_drawn(int imgid,int algorithm,int shape,const float *xy,int count,float radius,float border,float sx,float sy,int scale) {
    if(!xy || count<1 || count>2048 || (shape!=1 && shape!=3 && shape!=4) || (shape==4 && count<3)
        || algorithm<1 || algorithm>4 || scale<0 || scale>16 || !isfinite(radius) || radius<=0 || radius>.5f
        || !isfinite(border) || border<0 || border>.5f || !isfinite(sx) || !isfinite(sy) || load(imgid))return -1;
    for(int k=0;k<count*2;++k)if(!isfinite(xy[k]) || xy[k]<-2 || xy[k]>3)return -1;
    dt_iop_module_t *m=retouch_module();rt_form_data *forms=rt_forms_of(m);if(!forms)return -1;
    int slot=-1;for(int i=0;i<RT_NO_FORMS;++i)if(!forms[i].formid){slot=i;break;}if(slot<0)return -1;
    dt_masks_form_t *group=retouch_group(m,1);if(!group)return -1;
    const int kind=shape==1?DT_MASKS_CIRCLE:shape==3?DT_MASKS_BRUSH:DT_MASKS_PATH;
    dt_masks_form_t *form=dt_masks_create(kind|((algorithm<=2)?DT_MASKS_CLONE:DT_MASKS_NON_CLONE));
    if(shape==1)fill_shape(form,1,xy[0],xy[1],radius,border,0);
    else for(int i=0;i<count;++i) {
        if(shape==3) {
            dt_masks_point_brush_t *p=calloc(1,sizeof *p);p->corner[0]=xy[i*2];p->corner[1]=xy[i*2+1];
            p->ctrl1[0]=p->ctrl1[1]=p->ctrl2[0]=p->ctrl2[1]=-1;
            p->border[0]=p->border[1]=radius;p->hardness=fmaxf(.05f,1-border/radius);p->density=1;p->state=DT_MASKS_POINT_STATE_NORMAL;
            form->points=g_list_append(form->points,p);
        } else {
            dt_masks_point_path_t *p=calloc(1,sizeof *p);p->corner[0]=xy[i*2];p->corner[1]=xy[i*2+1];
            memcpy(p->ctrl1,p->corner,sizeof p->ctrl1);memcpy(p->ctrl2,p->corner,sizeof p->ctrl2);
            p->border[0]=p->border[1]=border;p->state=DT_MASKS_POINT_STATE_USER;form->points=g_list_append(form->points,p);
        }
    }
    if(shape==3)brush_init_ctrl_points(form);
    form->source[0]=sx;form->source[1]=sy;
    snprintf(form->name,sizeof form->name,"Retouch %d",(int)g_list_length(group->points)+1);
    g_dev.forms=g_list_append(g_dev.forms,form);
    dt_masks_point_group_t *entry=calloc(1,sizeof *entry);entry->formid=form->formid;entry->parentid=group->formid;entry->state=DT_MASKS_STATE_USE;entry->opacity=1;
    group->points=g_list_append(group->points,entry);const int index=g_list_length(group->points)-1;
    rt_fill(&forms[slot],form->formid,algorithm,10,0);forms[slot].scale=scale;
    ++g_dev.focus_hash;m->enabled=TRUE;
    dt_dev_add_masks_history_item_ext(&g_dev,m,TRUE,TRUE);dt_dev_write_history(&g_dev);reload();return index;
}
// This layout is checked against the pinned module's introspection before use.
typedef struct {
    int type, node_type, selected, hovered;
    int8_t prev, index, next, padding;
    float point[2], strength[2], radius[2], control1, control2;
    int warp_type, status;
    float ctrl1[2], ctrl2[2];
} oma_liquify_node;
static oma_liquify_node *liquify_nodes(dt_iop_module_t *m) {
    if (!m || !m->so->get_f || !m->so->get_p) return NULL;
    dt_introspection_field_t *f=m->so->get_f("nodes");
    if (!f || f->header.type!=DT_INTROSPECTION_TYPE_ARRAY || f->Array.count!=100
        || f->Array.field->header.size!=sizeof(oma_liquify_node) || m->params_size!=100*sizeof(oma_liquify_node)) return NULL;
    return m->so->get_p(m->params,"nodes");
}
int oma_engine_liquify_get(int imgid, float *warps, int capacity, int *enabled) {
    if (load(imgid)) return -1;
    dt_iop_module_t *m=find_module("liquify"); oma_liquify_node *nodes=liquify_nodes(m);
    if (!nodes) { set_error("unsupported liquify layout",NULL); return -1; }
    if (enabled) *enabled=m->enabled;
    const float width=g_dev.image_storage.width, height=g_dev.image_storage.height;
    if(width<=0 || height<=0) return -1;
    int n=0;
    for(int i=0;i<100 && nodes[i].type;++i) {
        // All OmaRAW warps are independent points. Do not silently flatten
        // paths authored by a different application.
        if(nodes[i].type!=1 || nodes[i].prev!=-1 || nodes[i].next!=-1) return -1;
        if(warps && n<capacity) {
            float *v=warps+n*8; v[0]=nodes[i].warp_type;
            for(int k=0;k<2;++k) {
                const float d=k?height:width;
                v[1+k]=nodes[i].point[k]/d; v[3+k]=nodes[i].strength[k]/d; v[5+k]=nodes[i].radius[k]/d;
            }
            v[7]=nodes[i].control1;
        }
        ++n;
    }
    return n;
}
int oma_engine_liquify_set(int imgid, const float *warps, int count, int enabled) {
    if(count<0 || count>100 || (count && !warps) || load(imgid)) return -1;
    dt_iop_module_t *m=find_module("liquify"); oma_liquify_node *nodes=liquify_nodes(m);
    if(!nodes) return -1;
    oma_liquify_node next[100]={0};
    const float width=g_dev.image_storage.width, height=g_dev.image_storage.height;
    for(int i=0;i<count;++i) {
        const float *v=warps+i*8;
        for(int k=0;k<8;++k) if(!isfinite(v[k]) || fabsf(v[k])>8) { set_error("invalid liquify point",NULL); return -1; }
        if(v[0]<0 || v[0]>2 || v[0]!=(int)v[0] || v[7]<0 || v[7]>1) return -1;
        oma_liquify_node *n=&next[i]; n->type=1; n->node_type=3; n->index=i; n->prev=n->next=-1;
        n->warp_type=(int)v[0]; n->control1=v[7]; n->control2=.75f;
        for(int k=0;k<2;++k) { const float d=k?height:width; n->point[k]=v[1+k]*d; n->strength[k]=v[3+k]*d; n->radius[k]=v[5+k]*d; }
        if(hypotf(n->radius[0]-n->point[0],n->radius[1]-n->point[1])<.5f) return -1;
    }
    memcpy(nodes,next,sizeof next); m->enabled=enabled && count>0;
    dt_dev_add_history_item_ext(&g_dev,m,m->enabled,TRUE); dt_dev_write_history(&g_dev);
    return 0;
}
int oma_engine_liquify_add(int imgid, int type, float x, float y, float dx, float dy, float radius, float strength) {
    float warps[800]; int enabled=0;
    const int n=oma_engine_liquify_get(imgid,warps,100,&enabled);
    if(n<0 || n>=100) { set_error("Liquify supports up to 100 points",NULL); return -1; }
    if(type<0 || type>2 || !isfinite(x) || !isfinite(y) || !isfinite(dx) || !isfinite(dy)
        || !isfinite(radius) || !isfinite(strength) || x<0 || x>1 || y<0 || y>1 || radius<.001f || radius>.5f || strength<0 || strength>1) return -1;
    oma_preview_cache *cache=prepare_geometry_preview(imgid,0);
    if(!cache) return -1;
    const float w=cache->pipe.processed_width,h=cache->pipe.processed_height;
    const float r=radius*fminf(w,h);
    float points[6]={x*w,y*h,x*w+dx*w*strength,y*h+dy*h*strength,x*w+r,y*h};
    if(type) { points[2]=points[0]+r*strength*.5f; points[3]=points[1]; }
    dt_iop_module_t *m=find_module("liquify");
    // Follow darktable's path editor: coordinates exclude Liquify's own warp.
    dt_dev_distort_backtransform_plus(&cache->dev,&cache->pipe,m->iop_order,DT_DEV_TRANSFORM_DIR_FORW_EXCL,points,3);
    dt_dev_distort_backtransform_plus(&cache->dev,&cache->pipe,m->iop_order,DT_DEV_TRANSFORM_DIR_BACK_EXCL,points,3);
    float *v=warps+n*8; v[0]=type;
    for(int k=0;k<6;++k) v[k+1]=points[k]/(k%2?g_dev.image_storage.height:g_dev.image_storage.width);
    v[7]=.5f;
    ++g_dev.focus_hash;
    return oma_engine_liquify_set(imgid,warps,n+1,1);
}
int oma_engine_raster_get(int imgid, const char *target, int *inverse, float *opacity) {
    if(load(imgid)) return -1;
    dt_iop_module_t *m=find_module(target);
    if(!m || !m->blend_params) return 0;
    const dt_develop_blend_params_t *b=m->blend_params;
    if(!(b->mask_mode&DEVELOP_MASK_RASTER) || strcmp(b->raster_mask_source,"rasterfile")) return 0;
    if(inverse) *inverse=b->raster_mask_invert;
    if(opacity) *opacity=b->opacity/100.f;
    return 1;
}
int oma_engine_raster_set(int imgid, const char *path, const char *target, int inverse, float opacity) {
    if(!path || !target || !isfinite(opacity) || opacity<0 || opacity>1 || load(imgid)) return -1;
    dt_iop_module_t *source=find_module("rasterfile"),*m=find_module(target);
    if(!source || !m || !m->blend_params || m->iop_order<=source->iop_order) { set_error("invalid external mask target",NULL); return -1; }
    const gboolean on=path[0]!=0;
    if(on && !g_file_test(path,G_FILE_TEST_IS_REGULAR)) { set_error("mask file unavailable: %s",path); return -1; }
    int pn=0,fn=0;
    char *folder=char_array_field(source,"path",&pn),*file=char_array_field(source,"file",&fn);
    if(!folder || !file) return -1;
    gchar *dir=g_path_get_dirname(path),*base=g_path_get_basename(path);
    if(on && (strlen(dir)>=(size_t)pn || strlen(base)>=(size_t)fn)) { g_free(dir);g_free(base);return -1; }
    ++g_dev.focus_hash;
    if(on) {
        g_strlcpy(folder,dir,pn);g_strlcpy(file,base,fn);
        source->enabled=TRUE;
        oma_add_history_item(&g_dev,source,TRUE,TRUE);
    }
    g_free(dir);g_free(base);
    dt_develop_blend_params_t blend=*m->blend_params;
    if(on) {
        blend.mask_mode=DEVELOP_MASK_ENABLED|DEVELOP_MASK_RASTER;
        g_strlcpy(blend.raster_mask_source,"rasterfile",sizeof blend.raster_mask_source);
        blend.raster_mask_instance=source->multi_priority;blend.raster_mask_id=BLEND_RASTER_ID;
        blend.raster_mask_invert=inverse!=0;blend.opacity=opacity*100;
    } else {
        blend.mask_mode=DEVELOP_MASK_DISABLED;blend.raster_mask_source[0]=0;
        blend.raster_mask_instance=0;blend.raster_mask_id=INVALID_MASKID;
    }
    dt_iop_commit_blend_params(m,&blend,NULL);
    oma_add_history_item(&g_dev,m,on?TRUE:m->enabled,TRUE);dt_dev_write_history(&g_dev);
    clear_preview_cache();
    return 0;
}
int oma_engine_levels_set(int imgid,int channel,float black,float middle,float white) {
    if(channel<0 || channel>2 || !isfinite(black) || !isfinite(middle) || !isfinite(white)
        || black<0 || white>1 || middle-black<.00001f || white-middle<.00001f || load(imgid)) return -1;
    dt_iop_module_t *m=find_module("rgblevels"); dt_introspection_field_t *f=NULL;
    char field[32];snprintf(field,sizeof field,"levels[%d]",channel);
    float *v=numeric_field(m,field,&f);
    if(!v || f->header.type!=DT_INTROSPECTION_TYPE_ARRAY || f->Array.count!=3) return -1;
    v[0]=black;v[1]=middle;v[2]=white;
    dt_dev_add_history_item_ext(&g_dev,m,TRUE,TRUE);dt_dev_write_history(&g_dev);return 0;
}
