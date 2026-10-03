// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagematchservice.h"
#include "engineservice.h"
#include "colourpipeline.h"
#include "displaycolour.h"
#include <QImageReader>
#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

namespace {
ImageMatch::Source sourceData(const QVariantMap &m) {
    ImageMatch::Source source;source.overview=m.value("image").value<QImage>();
    source.fullSize=QSize(m.value("fullWidth").toInt(),m.value("fullHeight").toInt());
    for(const auto &v:m.value("tiles").toList()) source.grainTiles<<v.value<QImage>();
    return source;
}
QString key(const QVariantMap &m) { return m.value("path").toString()+QLatin1Char('#')+QString::number(m.value("variant").toInt()); }
}
ImageMatchService::ImageMatchService(EngineService *engine):QObject(engine),m_engine(engine) {
    m_pool.setMaxThreadCount(1);m_pool.setThreadPriority(QThread::LowPriority);
    m_options=ImageMatch::Options::fromMap(QJsonDocument::fromJson(QSettings().value("imageMatch/options").toByteArray()).object().toVariantMap());
    connect(engine,&EngineService::imageMatchReady,this,&ImageMatchService::response);
    connect(engine,&EngineService::imageChanged,this,[this] {
        if(m_batch || m_busy || m_targetItem.isEmpty()) return;
        if(m_targetItem.value("path").toString()!=m_engine->imagePath() || m_targetItem.value("variant").toInt()!=m_engine->imageVariant()) invalidate();
    });
    connect(&DisplayColour::instance(),&DisplayColour::changed,this,[this]{++m_serial;emit changed();});
    connect(&ColourPipeline::instance(),&ColourPipeline::changed,this,[this]{++m_serial;emit changed();});
}
ImageMatchService::~ImageMatchService() {++*m_generation;m_pool.clear();m_pool.waitForDone();}
QString ImageMatchService::source(const QString &which) const {return QStringLiteral("image://engine/_match/%1/%2").arg(which).arg(m_serial);}
QString ImageMatchService::referenceSource() const {return hasReference()?source(m_detail?"referenceDetail":"reference"):QString();}
QString ImageMatchService::before() const {return m_previewReady?source(m_detail?"beforeDetail":"before"):QString();}
QString ImageMatchService::after() const {return m_previewReady?source(m_detail?"afterDetail":"after"):QString();}
QImage ImageMatchService::image(const QString &which) const {
    QMutexLocker lock(&m_mutex);
    if(which=="reference")return m_referenceView;
    if(which=="before")return m_beforeView;
    if(which=="after")return m_afterView;
    if(which=="referenceDetail")return m_referenceDetail;
    if(which=="beforeDetail")return m_beforeDetail;
    if(which=="afterDetail")return m_afterDetail;
    return {};
}
void ImageMatchService::invalidate() {
    m_previewReady=false;m_previewApplied=false;m_detail=false;m_manualGrain=false;m_target={};m_targetItem.clear();m_sourceData.clear();m_report.clear();m_area.clear();
    {QMutexLocker lock(&m_mutex);m_beforeView={};m_afterView={};m_beforeDetail={};m_afterDetail={};}
    ++m_serial;emit changed();
}
bool ImageMatchService::ready() {
    if(m_busy)return false;
    if(!hasReference()){m_status=tr("Choose a reference photograph first.");emit changed();return false;}
    if(!m_engine->ready() || m_engine->imageId()<0 || !m_engine->maintenanceReady()) {m_status=tr("Wait for the current edit to finish.");emit changed();return false;}
    return true;
}
void ImageMatchService::request(const QString &action,const QVariantMap &data) {m_engine->requestImageMatch(m_ticket,action,data,m_generation);}
void ImageMatchService::setReference(const QString &fileOrUrl) {
    if(m_busy)return;
    const QUrl url(fileOrUrl);const QString path=url.isLocalFile()?url.toLocalFile():fileOrUrl;
    const QString suffix=QFileInfo(path).suffix().toLower();
    if(!QStringList{"jpg","jpeg","png","tif","tiff","webp"}.contains(suffix)) {m_status=tr("Choose a JPEG, PNG, TIFF or WebP, or use the edited photograph currently open in Develop.");emit changed();emit operationFailed(m_status);return;}
    const auto files=std::make_shared<QTemporaryDir>();if(!files->isValid()){fail(tr("Could not create the reference cache."));return;}
    m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_action="referenceFile";m_status=tr("Reading the reference and its colour profile…");emit changed();
    const int ticket=m_ticket;const auto cancel=m_generation;
    m_pool.start([this,path,suffix,files,ticket,cancel] {
        QString error;ImageMatch::Source prepared;
        const QFileInfo original(path);const QString frozen=files->filePath("reference."+suffix);
        if(!original.isFile() || original.size()>512LL*1024*1024 || !QFile::copy(path,frozen)) error=tr("Could not read this reference (maximum file size 512 MB).");
        else {
            QImageReader reader(frozen);reader.setAutoTransform(true);
            const QSize dimensions=reader.size();
            if(!dimensions.isValid() || qint64(dimensions.width())*dimensions.height()>64000000) error=tr("Use a reference of at most 64 megapixels.");
            else if(cancel->load()==ticket) {
                const QImage image=reader.read();
                if(image.isNull())error=reader.errorString();else prepared=ImageMatch::fromImage(image);
            }
            if(original.size()!=QFileInfo(path).size() || original.lastModified()!=QFileInfo(path).lastModified()) error=tr("The reference changed while being read. Choose it again.");
        }
        QMetaObject::invokeMethod(this,[this,ticket,files,frozen,path,prepared,error] {
            if(ticket!=m_ticket)return;
            if(m_cancelled){complete();return;}if(!error.isEmpty()){fail(error);return;}
            m_referenceFiles=files;m_referencePath=frozen;m_referenceItem.clear();m_reference=prepared;m_referenceName=QFileInfo(path).fileName();
            {QMutexLocker lock(&m_mutex);m_referenceView=prepared.overview;m_referenceDetail={};}
            invalidate();m_status=tr("Reference ready. Choose the target photo or a selection.");request("clearReference");emit changed();
        },Qt::QueuedConnection);
    });
}
void ImageMatchService::useCurrent() {
    if(m_busy || m_engine->imageId()<0 || !m_engine->maintenanceReady())return;
    m_ticket=++*m_generation;m_cancelled=false;m_busy=true;m_action="reference";
    m_targetItem={{"path",m_engine->imagePath()},{"variant",m_engine->imageVariant()}};
    m_status=tr("Capturing the edited reference and native detail…");emit changed();request("reference",m_targetItem);
}
void ImageMatchService::clearReference() {
    if(m_busy)return;
    m_ticket=++*m_generation;request("clearReference");m_reference={};m_referenceFiles.reset();m_referencePath.clear();m_referenceItem.clear();m_referenceName.clear();
    {QMutexLocker lock(&m_mutex);m_referenceView={};m_referenceDetail={};}
    invalidate();m_status.clear();emit changed();
}
void ImageMatchService::setOptions(const QVariantMap &options) {
    if(m_busy)return;
    const auto next=ImageMatch::Options::fromMap(options);if(next.toMap()==m_options.toMap())return;
    const auto previous=m_options;m_options=next;
    QSettings().setValue("imageMatch/options",QJsonDocument(QJsonObject::fromVariantMap(next.toMap())).toJson(QJsonDocument::Compact));
    emit changed();
    if(!m_previewReady || m_sourceData.isEmpty())return;
    m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_batch=false;m_action="preview";m_detail=false;
    if(previous.creative!=next.creative || previous.exposure!=next.exposure || previous.whiteBalance!=next.whiteBalance
       || previous.tone!=next.tone || previous.colour!=next.colour || previous.grain!=next.grain
       || previous.protectSkin!=next.protectSkin) m_manualGrain=false;
    // Exposure and WB depend only on the immutable source/reference, never
    // on creative strengths. Recheck residual grain after the changed tone.
    m_runOptions=next;analyseTarget(m_sourceData);
    emit changed();
}
void ImageMatchService::preview() {
    if(!ready())return;
    invalidate();m_ticket=++*m_generation;m_busy=true;m_batch=false;m_cancelled=false;m_undoing=false;m_action="preview";m_runOptions=m_options;
    m_targetItem={{"path",m_engine->imagePath()},{"variant",m_engine->imageVariant()}};m_status=tr("Analysing this photo against the reference…");emit changed();request("source",m_targetItem);
}
void ImageMatchService::analyseTarget(const QVariantMap &result) {
    m_sourceData=result;m_target=sourceData(result);
    if(m_target.overview.isNull()){fail(tr("The target preview could not be read."));return;}
    {QMutexLocker lock(&m_mutex);m_beforeView=result.value("before").value<QImage>();}
    const auto reference=m_reference,target=m_target;const auto options=m_runOptions;const int ticket=m_ticket;const auto cancel=m_generation;
    m_status=tr("Matching tone and colour; checking native grain samples…");emit changed();
    m_pool.start([this,reference,target,options,ticket,cancel] {
        const auto analysis=ImageMatch::analyse(reference,target,options,cancel.get(),ticket);
        QMetaObject::invokeMethod(this,[this,ticket,analysis] {
            if(ticket!=m_ticket)return;
            if(m_cancelled){complete();return;}
            const auto prior=m_result.params;m_result=analysis;
            if(m_manualGrain) {
                m_result.params.grain_amount=prior.grain_amount;m_result.params.grain_size=prior.grain_size;
                m_result.params.grain_roughness=prior.grain_roughness;m_result.params.grain_colour=prior.grain_colour;
                m_result.params.grain_texture=prior.grain_texture;
                m_result.params.grain_shadows=prior.grain_shadows;m_result.params.grain_midtones=prior.grain_midtones;m_result.params.grain_highlights=prior.grain_highlights;
                m_result.warnings<<tr("Your manual grain settings are retained.");
            }
            m_report=m_result.report();
            if(analysis.confidence<=0){fail(analysis.warnings.join(' '));return;}
            QVariantMap data=m_sourceData;data["parameters"]=ImageMatch::parameters(m_result.params);
            request(m_batch?"apply":"preview",data);emit changed();
        },Qt::QueuedConnection);
    });
}
void ImageMatchService::publishPreview(const QVariantMap &result) {
    {QMutexLocker lock(&m_mutex);m_afterView=result.value("image").value<QImage>();}
    m_previewReady=!m_afterView.isNull();m_previewApplied=false;m_detail=false;m_busy=false;m_progress=1;++m_serial;
    m_area={{"frameWidth",m_target.fullSize.width()},{"frameHeight",m_target.fullSize.height()}};
    m_status=m_previewReady?tr("Preview ready. Compare at Fit or 100%, then Apply. Your existing edits are preserved."):tr("Could not render the match preview.");emit changed();
    if(!m_previewReady) emit operationFailed(m_status);
}
void ImageMatchService::apply() {
    if(!ready() || !m_previewReady)return;
    if(m_targetItem.value("path").toString()!=m_engine->imagePath() || m_targetItem.value("variant").toInt()!=m_engine->imageVariant()) {invalidate();m_status=tr("Preview this photo first.");emit changed();return;}
    m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_batch=false;m_undoing=false;m_action="apply";
    QVariantMap data=m_sourceData;data["parameters"]=ImageMatch::parameters(m_result.params);m_status=tr("Applying the editable match…");emit changed();request("apply",data);
}
void ImageMatchService::applySelection(const QVariantList &items) {
    if(!ready())return;
    QVariantList unique;QSet<QString> seen;
    for(const auto &v:items) {const auto item=v.toMap();const QString k=key(item);if(item.value("path").toString().isEmpty() || seen.contains(k) || (!m_referenceItem.isEmpty() && k==key(m_referenceItem)))continue;seen.insert(k);unique<<item;}
    if(unique.isEmpty()){m_status=tr("Select target photographs. The edited reference itself is skipped.");emit changed();emit operationFailed(m_status);return;}
    invalidate();m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_batch=true;m_undoing=false;m_action="batch";m_runOptions=m_options;
    m_items=unique;m_batchUndo.clear();m_batchResults.clear();m_done=m_failed=m_next=0;m_progress=0;next();
}
void ImageMatchService::next() {
    if(m_cancelled || m_next>=m_items.size()){complete();return;}
    m_targetItem=m_items[m_next++].toMap();m_progress=(m_next-1)/double(m_items.size());
    m_status=m_undoing?tr("Undoing match %1 of %2…").arg(m_next).arg(m_items.size()):tr("Matching %1 of %2 — %3").arg(m_next).arg(m_items.size()).arg(QFileInfo(m_targetItem.value("path").toString()).fileName());emit changed();
    request(m_undoing?"undo":"source",m_targetItem);
}
void ImageMatchService::undoBatch() {
    if(!canUndoBatch())return;
    m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_batch=true;m_undoing=true;m_action="undo";
    m_items=m_batchUndo;std::reverse(m_items.begin(),m_items.end());m_batchUndo.clear();m_batchResults.clear();m_done=m_failed=m_next=0;next();
}
void ImageMatchService::response(int ticket,const QString &action,const QVariantMap &result,const QString &error) {
    if(ticket!=m_ticket)return;
    if(action=="clearReference") {
        if(m_action=="referenceFile") {m_busy=false;emit changed();}
        return;
    }
    if((action=="apply" || action=="undo") && result.value("committed").toBool()) {
        if(m_batch) {
            ++m_done;QVariantMap row={{"path",result.value("path")},{"ok",true},{"report",m_report}};m_batchResults<<row;
            if(!m_undoing)m_batchUndo<<QVariantMap{{"path",result.value("path")},{"variant",result.value("variant")},{"signature",result.value("signature")},{"beforeEnd",result.value("beforeEnd")}};
            next();
        } else {
            m_sourceData["signature"]=result.value("signature");m_previewApplied=true;m_busy=false;m_status=m_cancelled?tr("Match was applied before cancellation. Undo restores the previous result."):tr("Image Match applied. Switches, strengths and grain controls now adjust this photo directly; Undo restores the previous result.");emit changed();emit finished(1,0,m_cancelled);
        }
        return;
    }
    if(m_cancelled){complete();return;}
    if(!m_batch && (action=="source" || action=="preview" || action=="detail" || action=="referenceDetail")
        && (m_targetItem.value("path").toString()!=m_engine->imagePath() || m_targetItem.value("variant").toInt()!=m_engine->imageVariant())) {
        m_ticket=++*m_generation;invalidate();m_busy=false;m_status=tr("The selected photo changed. Preview its match again.");emit changed();return;
    }
    if(!error.isEmpty()){fail(error);return;}
    if(action=="reference") {
        m_reference=sourceData(result);m_referenceItem=m_targetItem;m_referenceName=QFileInfo(m_targetItem.value("path").toString()).fileName()+tr(" · edited");m_referencePath.clear();m_referenceFiles.reset();
        {QMutexLocker lock(&m_mutex);m_referenceView=m_reference.overview;m_referenceDetail={};}
        invalidate();m_busy=false;m_status=tr("Edited reference captured. Select another photo or a batch to match.");emit changed();
    } else if(action=="source") analyseTarget(result);
    else if(action=="preview") publishPreview(result);
    else if(action=="detail") {
        {QMutexLocker lock(&m_mutex);m_afterDetail=result.value("image").value<QImage>();m_beforeDetail=result.value("before").value<QImage>();}
        m_area={{"x",result.value("x")},{"y",result.value("y")},{"width",m_afterDetail.width()},{"height",m_afterDetail.height()},{"frameWidth",result.value("fullWidth")},{"frameHeight",result.value("fullHeight")}};
        if(--m_detailPending==0){m_detail=true;m_busy=false;m_status=tr("Native 100% detail ready.");++m_serial;emit changed();}
    } else if(action=="referenceDetail") {
        {QMutexLocker lock(&m_mutex);m_referenceDetail=result.value("image").value<QImage>();}
        if(--m_detailPending==0){m_detail=true;m_busy=false;m_status=tr("Native 100% detail ready.");++m_serial;emit changed();}
    }
}
void ImageMatchService::fail(const QString &error) {
    emit operationFailed(error.isEmpty()?tr("Image Match could not finish. Try a different reference."):error);
    if(m_batch && m_busy) {++m_failed;m_batchResults<<QVariantMap{{"path",m_targetItem.value("path")},{"ok",false},{"error",error}};next();return;}
    m_busy=false;m_previewReady=false;m_status=error.isEmpty()?tr("Image Match could not finish. Try a different reference."):error;emit changed();
}
bool ImageMatchService::savePreview(const QString &path,const QString &which) {
    if(m_busy || !QStringList{"reference","before","after","referenceDetail","beforeDetail","afterDetail"}.contains(which)) {
        emit operationFailed(tr("Choose a ready Image Match reference, before or after preview.")); return false;
    }
    const QImage frame=image(which);
    if(frame.isNull()) { emit operationFailed(tr("That Image Match preview is not available in this run.")); return false; }
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) { emit operationFailed(tr("Preview needs a new writable PNG path: %1").arg(file.errorString())); return false; }
    if(!ColourPipeline::srgb8(frame).save(&file,"PNG")) {
        file.close();file.remove();emit operationFailed(tr("Could not write the Image Match preview."));return false;
    }
    return true;
}
void ImageMatchService::complete() {
    const bool batch=m_batch,undoing=m_undoing;m_busy=false;m_batch=false;m_undoing=false;m_progress=1;
    if(batch) {m_previewReady=false;m_status=m_cancelled?tr("Stopped. %1 photos completed; %2 failed. Completed matches can be undone.").arg(m_done).arg(m_failed):undoing?tr("Undid %1 matches; %2 photos were skipped because they changed or became unavailable.").arg(m_done).arg(m_failed):tr("Matched %1 photos individually; %2 failed. Each match is editable and undoable.").arg(m_done).arg(m_failed);emit finished(m_done,m_failed,m_cancelled);}
    else m_status=tr("Match cancelled. No pending preview was applied.");
    emit changed();
}
void ImageMatchService::cancel() {
    if(!m_busy)return;
    m_cancelled=true;++*m_generation;m_status=tr("Stopping Image Match…");emit changed();
}
void ImageMatchService::reset() {
    if(m_busy)return;
    invalidate();m_engine->resetTool("omarawmatch");m_status=tr("Image Match reset. Other tools keep their edits; Undo restores the match.");emit changed();
}
void ImageMatchService::showFit() {
    if(m_busy)return;
    m_detail=false;++m_serial;emit changed();
}
void ImageMatchService::detailAt(double x,double y) {
    if(m_busy || !m_previewReady || !std::isfinite(x+y))return;
    m_x=qBound(0.,x,1.);m_y=qBound(0.,y,1.);m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_action="detail";m_detailPending=2;
    QVariantMap data=m_sourceData;data["parameters"]=ImageMatch::parameters(m_result.params);data["x"]=m_x;data["y"]=m_y;
    m_status=tr("Rendering native 100% detail…");emit changed();request("detail",data);
    if(m_referencePath.isEmpty()) request("referenceDetail",{{"x",m_x},{"y",m_y}});
    else {
        const auto files=m_referenceFiles;const QString path=m_referencePath;const int ticket=m_ticket;const double px=m_x,py=m_y;const auto cancel=m_generation;
        m_pool.start([this,files,path,ticket,px,py,cancel] {
            QImage detail;QString error;
            if(cancel->load()==ticket) {QImageReader reader(path);reader.setAutoTransform(true);const QImage full=reader.read();if(full.isNull())error=reader.errorString();else {const int w=qMin(640,full.width()),h=qMin(640,full.height());detail=ColourPipeline::linear(full.copy(qBound(0,int(px*full.width())-w/2,full.width()-w),qBound(0,int(py*full.height())-h/2,full.height()-h),w,h));}}
            QMetaObject::invokeMethod(this,[this,ticket,detail,error]{response(ticket,"referenceDetail",{{"image",detail}},error);},Qt::QueuedConnection);
        });
    }
}
void ImageMatchService::editDraft(const QString &field,double value) {
    if(m_busy || !m_previewReady || !std::isfinite(value))return;
    auto values=ImageMatch::parameters(m_result.params);if(!values.contains(field))return;values[field]=value;
    oma_match_params params;if(!ImageMatch::parameters(values,&params))return;
    if(field=="grain_amount" && value>0 && !m_options.grain) {
        m_options.grain=true;params.grain_on=1;params.grain_strength=m_options.grainStrength;
        QSettings().setValue("imageMatch/options",QJsonDocument(QJsonObject::fromVariantMap(m_options.toMap())).toJson(QJsonDocument::Compact));
    }
    m_result.params=params;m_result.fit.clear();m_manualGrain=field.startsWith("grain_") || m_manualGrain;m_report=m_result.report();m_ticket=++*m_generation;m_busy=true;m_cancelled=false;m_detail=false;m_action="preview";
    QVariantMap data=m_sourceData;data["parameters"]=ImageMatch::parameters(params);request("preview",data);emit changed();
}

void ImageMatchService::editApplied(const QString &field,double value) {
    if(m_busy || !m_engine->ready() || m_engine->imageId()<0 || !std::isfinite(value))return;
    const auto rows=m_engine->paramsFor("omarawmatch");
    for(const auto &entry:rows) {
        const auto row=entry.toMap();
        if(row.value("field").toString()!=field || (!row.value("enabled").toBool() && !row.value("configured").toBool()))continue;
        // Saved adjustments use the ordinary live-edit path and its gesture
        // undo. Discard the old comparison so it cannot reapply stale settings.
        if(m_previewReady)invalidate();
        m_engine->setParam("omarawmatch",field,qBound(row.value("min").toDouble(),value,row.value("max").toDouble()));
        m_status=tr("Applied match updated. Undo restores the previous adjustment.");emit changed();return;
    }
}
