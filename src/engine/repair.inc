// AI inputs and previews use private contexts. Never park live history to
// prepare a removal: Undo, snapshots and the current edit remain untouched.
static dt_dev_pixelpipe_iop_t *repair_piece(oma_preview_cache *cache) {
    if(cache) for(GList *l=cache->pipe.nodes;l;l=l->next) {
        dt_dev_pixelpipe_iop_t *p=l->data;
        if(!strcmp(p->module->op,"omarawrepair")) return p;
    }
    return NULL;
}
static void *repair_symbol(dt_dev_pixelpipe_iop_t *piece,const char *name) {
    void *fn=NULL;
    if(piece && piece->module->so->module) g_module_symbol(piece->module->so->module,name,&fn);
    return fn;
}
int oma_engine_repair_source(int imgid,const char *path,const char *mask_path,
                             const uint8_t *mask,int mw,int mh,int stride) {
    if(!path || !mask_path || !mask || mw<1 || mh<1 || mw>2048 || mh>2048 || stride<mw) return -1;
    oma_preview_cache *cache=prepare_geometry_preview(imgid,0);
    dt_dev_pixelpipe_iop_t *piece=repair_piece(cache);
    if(!piece) { set_error("AI repairs need the updated OmaRAW engine",NULL); return -1; }
    const int width=piece->buf_in.width,height=piece->buf_in.height;
    if(width<1 || height<1 || (uint64_t)width*height>110000000) { set_error("AI removal supports up to 110 megapixels",NULL); return -1; }
    FILE *f=g_fopen(mask_path,"wb");
    if(!f) { set_error("Cannot prepare the removal mask",NULL); return -1; }
    fprintf(f,"P5\n%d %d\n255\n",width,height);
    float *points=malloc((size_t)width*2*sizeof(float)); uint8_t *row=malloc(width);
    int rc=points && row ? 0 : -1;
    for(int y=0;!rc && y<height;y++) {
        for(int x=0;x<width;x++) { points[x*2]=x+.5f; points[x*2+1]=y+.5f; }
        if(!dt_dev_distort_transform_plus(&cache->dev,&cache->pipe,piece->module->iop_order,
                                          DT_DEV_TRANSFORM_DIR_FORW_EXCL,points,width)) { rc=-1; break; }
        for(int x=0;x<width;x++) {
            const float px=points[x*2]*mw/cache->pipe.processed_width,py=points[x*2+1]*mh/cache->pipe.processed_height;
            row[x]=isfinite(px)&&isfinite(py)&&px>=0&&py>=0&&px<mw&&py<mh ? mask[(int)py*stride+(int)px] : 0;
        }
        if(fwrite(row,1,width,f)!=(size_t)width) rc=-1;
    }
    free(points);free(row);if(fclose(f))rc=-1;
    int (*capture)(dt_dev_pixelpipe_iop_t*,const char*)=repair_symbol(piece,"omaraw_repair_capture");
    const char *(*error)(dt_dev_pixelpipe_iop_t*)=repair_symbol(piece,"omaraw_repair_error");
    if(!capture || !error || !capture(piece,path)) {set_error("AI repair capture is unavailable",NULL);rc=-1;}
    // A bypassed repair must not reappear when making a new one.
    if(!piece->enabled) {
        char *key=piece->module->so->get_p(piece->module->params,"file");
        if(key) key[0]=0;
        piece->module->commit_params(piece->module,piece->module->params,&cache->pipe,piece);
        if(capture) capture(piece,path);
    }
    piece->enabled=TRUE;
    for(GList *l=cache->pipe.nodes;l;l=l->next) {
        dt_dev_pixelpipe_iop_t *p=l->data;
        if(p->module->iop_order>piece->module->iop_order) p->enabled=FALSE;
    }
    dt_dev_pixelpipe_cache_flush(&cache->pipe);
    preview_dimensions(cache);
    dt_mipmap_buffer_t input;
    dt_mipmap_cache_get(&input,imgid,DT_MIPMAP_FULL,DT_MIPMAP_BLOCKING,'r');
    if(!input.buf) rc=-1;
    if(!rc) {
        dt_dev_pixelpipe_set_input(&cache->pipe,&cache->dev,(float*)input.buf,input.width,input.height,input.iscale);
        rc=dt_dev_pixelpipe_process_no_gamma(&cache->pipe,&cache->dev,0,0,width,height,1.f);
        if(error(piece)[0]) { set_error("%s",error(piece)); rc=-1; }
    }
    cache->pipe.input=NULL;dt_mipmap_cache_release(&input);clear_preview(cache);
    if(rc) { g_unlink(path); g_unlink(mask_path); if(!g_error[0])set_error("Cannot prepare the editable AI source",NULL); }
    return rc ? -1 : 0;
}
static int repair_context_valid(oma_preview_cache *cache) {
    dt_dev_pixelpipe_iop_t *piece=repair_piece(cache);
    if(!piece || !piece->enabled) return 0;
    const char *(*error)(dt_dev_pixelpipe_iop_t*)=repair_symbol(piece,"omaraw_repair_error");
    if(!error || error(piece)[0]) { set_error("%s",error?error(piece):"AI repair engine is unavailable"); return -1; }
    return 0;
}

int oma_engine_repair_check(int imgid) { return repair_context_valid(prepare_geometry_preview(imgid,0)); }

static void repair_resources_key(GChecksum *sum,const char *key) {
    char next[65];g_strlcpy(next,key,sizeof next);
    char *folder=dt_conf_get_string("plugins/darkroom/omarawrepair/directory");
    for(int n=0;next[0] && n<4096;n++) {
        if(strlen(next)!=64) break;
        gboolean safe=TRUE;for(int i=0;i<64;i++) if(!g_ascii_isxdigit(next[i]))safe=FALSE;
        if(!safe) break;
        char *name=g_strconcat(next,".orp",NULL),*path=g_build_filename(folder,name,NULL);g_free(name);
        g_checksum_update(sum,(const guchar*)path,strlen(path));
        GStatBuf st={0};int rc=g_stat(path,&st);
        g_checksum_update(sum,(const guchar*)&rc,sizeof rc);
        if(!rc) {
            g_checksum_update(sum,(const guchar*)&st.st_size,sizeof st.st_size);
            g_checksum_update(sum,(const guchar*)&st.st_mtim,sizeof st.st_mtim);
            g_checksum_update(sum,(const guchar*)&st.st_ctim,sizeof st.st_ctim);
        }
        FILE *f=g_fopen(path,"rb");g_free(path);if(!f)break;
        char header[96];const int ok=fread(header,1,sizeof header,f)==sizeof header;fclose(f);
        if(!ok)break;memcpy(next,header+32,64);next[64]=0;
    }
    g_free(folder);
}
