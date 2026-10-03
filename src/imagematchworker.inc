// Included in EngineWorker. Every native call stays on the serial engine
// thread. Private comparisons are journalled and never renumber variants.
    int matchReferenceImage=-1;
    QByteArray matchReferenceKey;
    void clearMatchReference() {
#ifdef OMARAW_ENGINE
        if(matchReferenceImage>=0) oma_engine_comparison_remove(matchReferenceImage);
#endif
        matchReferenceImage=-1;matchReferenceKey.clear();
    }
    QByteArray matchSignature(int id) {
#ifdef OMARAW_ENGINE
        char *key=oma_engine_match_signature(id);const QByteArray result=key?QByteArray(key):QByteArray();oma_engine_free(key);return result;
#else
        Q_UNUSED(id) return {};
#endif
    }
    QVariantMap readMatchImage(int id,bool samples,bool detail,double cx,double cy,QString *error) {
        QVariantMap result;
#ifdef OMARAW_ENGINE
        int fw=0,fh=0;
        if(oma_engine_preview_dimensions(id,0,0,&fw,&fh) || fw<=0 || fh<=0) { *error=QString::fromUtf8(oma_engine_error());return result; }
        result["fullWidth"]=fw;result["fullHeight"]=fh;
        float *data=nullptr;int w=0,h=0,rc=-1;QRect detailCrop;
        if(detail) {
            const int width=qMin(640,fw),height=qMin(640,fh);
            const int x=qBound(0,int(cx*fw)-width/2,fw-width),y=qBound(0,int(cy*fh)-height/2,fh-height);
            result["x"]=x;result["y"]=y;
            // Keep the requested detail away from the region boundary. Raw
            // interpolation can otherwise disagree with a full-size export
            // along the crop edge, even before a match is applied.
            const QRect expanded=QRect(x,y,width,height).adjusted(-16,-16,16,16).intersected(QRect(0,0,fw,fh));
            detailCrop=QRect(x-expanded.x(),y-expanded.y(),width,height);
            rc=oma_engine_render_region(id,expanded.x(),expanded.y(),expanded.width(),expanded.height(),1,&data,&w,&h);
        } else rc=oma_engine_render(id,1600,1600,&data,&w,&h);
        if(rc) {oma_engine_free(data);*error=rc==OMA_ENGINE_CANCELLED?tr("Match cancelled"):QString::fromUtf8(oma_engine_error());return {};}
        const QImage rendered=takeLinear(data,w,h);
        result["image"]=detail?rendered.copy(detailCrop):rendered;
        QVariantList tiles;
        if(samples) for(int y=0;y<3;++y) for(int x=0;x<3;++x) {
            const int side=qMin(192,qMin(fw,fh)); data=nullptr;w=h=0;
            rc=oma_engine_render_region(id,(fw-side)*x/2,(fh-side)*y/2,side,side,1,&data,&w,&h);
            if(rc) {oma_engine_free(data);*error=rc==OMA_ENGINE_CANCELLED?tr("Match cancelled"):QString::fromUtf8(oma_engine_error());return {};}
            tiles<<takeLinear(data,w,h);
        }
        result["tiles"]=tiles;
#else
        Q_UNUSED(id) Q_UNUSED(samples) Q_UNUSED(detail) Q_UNUSED(cx) Q_UNUSED(cy) *error=tr("Image Match needs the processing engine.");
#endif
        return result;
    }
    void matchRequest(int ticket,const QString &action,const QVariantMap &request,const std::shared_ptr<std::atomic<int>> &cancel) {
        QVariantMap result;QString error;
#ifdef OMARAW_ENGINE
        if(action=="clearReference") {clearMatchReference();emit imageMatchReady(ticket,action,result,error);return;}
        PreviewRequest scoped(cancel.get(),ticket);
        const QString path=request.value("path").toString();const int variant=request.value("variant").toInt();
        const bool refDetail=action=="referenceDetail";
        int id=refDetail?matchReferenceImage:oma_engine_open_version(path.toLocal8Bit().constData(),variant);
        int copy=-1;const auto cleanup=qScopeGuard([&]{if(copy>=0) oma_engine_comparison_remove(copy);});
        const auto failed=[&](int code) {if(code==0)return false;error=QString::fromUtf8(oma_engine_error());if(error.isEmpty())error=tr("Image Match could not render this photo.");return true;};
        if(scoped.cancelled()) error=tr("Match cancelled");
        else if(id<0) error=refDetail?tr("Choose a reference first."):QString::fromUtf8(oma_engine_error());
        else {
            const QByteArray signature=matchSignature(id);
            if((request.contains("signature") && signature!=request.value("signature").toByteArray())
                || (refDetail && signature!=matchReferenceKey)) error=tr("The photo changed after analysis. Preview the match again.");
            else if(action=="apply" || action=="undo") {
                result["path"]=path;result["variant"]=variant;
                result["beforeEnd"]=oma_engine_history_end(id);
                int rc=-1;
                if(action=="undo") rc=oma_engine_history_set_end(id,request.value("beforeEnd").toInt());
                else {
                    oma_match_params params;
                    if(!ImageMatch::parameters(request.value("parameters").toMap(),&params)) error=tr("Invalid match settings.");
                    else if(!scoped.cancelled()) rc=oma_engine_match_set(id,reinterpret_cast<const float *>(&params),sizeof(params)/sizeof(float),1);
                }
                if(error.isEmpty() && !failed(rc)) {
                    result["committed"]=true;result["afterEnd"]=oma_engine_history_end(id);result["signature"]=matchSignature(id);
                    // Once committed, a cancel only stops later photos. Report
                    // this edit even if its optional thumbnail was interrupted.
                    float *pixels=nullptr;int w=0,h=0;
                    if(!oma_engine_render(id,1024,1024,&pixels,&w,&h)) result["thumbnail"]=ColourPipeline::srgb8(takeLinear(pixels,w,h));
                    else oma_engine_free(pixels);
                    result["flags"]=editFlags(id);
                }
            } else if(refDetail) result=readMatchImage(id,false,true,request.value("x",.5).toDouble(),request.value("y",.5).toDouble(),&error);
            else {
                copy=oma_engine_comparison_create(id);
                if(copy<0) error=QString::fromUtf8(oma_engine_error());
                else if(action=="reference") {
                    result=readMatchImage(copy,true,false,.5,.5,&error);
                    if(error.isEmpty() && !scoped.cancelled()) {clearMatchReference();matchReferenceImage=copy;matchReferenceKey=matchSignature(copy);copy=-1;}
                } else {
                    const bool detail=action=="detail";
                    if(action=="source") {
                        QVariantMap previousParameters;
                        for(const auto &row:ImageMatch::controls()) {
                            const QString field=row.toMap().value("field").toString();oma_param_info info;
                            if(!oma_engine_param_get(id,"omarawmatch",field.toLatin1().constData(),&info) && info.found) previousParameters[field]=info.value;
                        }
                        const bool previousEnabled=oma_engine_module_enabled(id,"omarawmatch")>0;
                        if(oma_engine_module_enabled(id,"omarawmatch")>0) {
                            const auto before=readMatchImage(id,false,false,.5,.5,&error);
                            result["before"]=before.value("image");
                        }
                        oma_match_params none;oma_match_defaults(&none);
                        if(error.isEmpty() && !failed(oma_engine_match_set(copy,reinterpret_cast<const float *>(&none),sizeof(none)/sizeof(float),0))) {
                            const QVariant previous=result.value("before");result=readMatchImage(copy,true,false,.5,.5,&error);
                            result["before"]=previous.isValid()?previous:result.value("image");
                            result["beforeParameters"]=previousParameters;result["beforeEnabled"]=previousEnabled;
                        }
                    } else if(action=="preview" || detail) {
                        oma_match_params params;
                        if(!ImageMatch::parameters(request.value("parameters").toMap(),&params)) error=tr("Invalid match settings.");
                        else if(!failed(oma_engine_match_set(copy,reinterpret_cast<const float *>(&params),sizeof(params)/sizeof(float),1))) {
                            result=readMatchImage(copy,false,detail,request.value("x",.5).toDouble(),request.value("y",.5).toDouble(),&error);
                            if(detail && error.isEmpty()) {
                                oma_match_params previous;ImageMatch::parameters(request.value("beforeParameters").toMap(),&previous);
                                failed(oma_engine_match_set(copy,reinterpret_cast<const float *>(&previous),sizeof(previous)/sizeof(float),request.value("beforeEnabled").toBool()));
                                const auto before=readMatchImage(copy,false,true,request.value("x",.5).toDouble(),request.value("y",.5).toDouble(),&error);
                                result["before"]=before.value("image");
                            }
                        }
                    } else error=tr("Unknown Image Match request.");
                    result["path"]=path;result["variant"]=variant;result["signature"]=signature;
                }
            }
        }
#else
        Q_UNUSED(request) Q_UNUSED(cancel) error=tr("Image Match needs the processing engine.");
#endif
        emit imageMatchReady(ticket,action,result,error);
    }
