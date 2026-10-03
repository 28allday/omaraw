// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagematch.h"
#include "colourpipeline.h"
#include <QColorSpace>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ImageMatch {
namespace {
bool stopped(const std::atomic<int> *cancel,int ticket) { return cancel && cancel->load()!=ticket; }
double quantile(std::vector<double> v,double q) {
    if(v.empty()) return 0;
    const size_t i=std::min(v.size()-1,size_t(q*(v.size()-1)));
    std::nth_element(v.begin(),v.begin()+i,v.end()); return v[i];
}
double median(const std::vector<double> &v) { return quantile(v,.5); }
struct Pixel { float rgb[3],lab[3],y; int hue; };
void describe(Pixel &p) {
    oma_match_oklab(p.rgb,p.lab);
    p.y=oma_match_encode(qMax(0.f,oma_match_luma(p.rgb)));
    const double chroma=std::hypot(p.lab[1],p.lab[2])/qMax(.01f,p.lab[0]);
    double hue=std::atan2(p.lab[2],p.lab[1]); if(hue<0) hue+=2*M_PI;
    p.hue=chroma<.08?0:1+qMin(5,int(hue/(M_PI/3)));
}
std::vector<Pixel> pixels(const QImage &source,bool includeExtremes=false) {
    const QImage im=ColourPipeline::linear(source);
    std::vector<Pixel> out; if(im.isNull()) return out;
    const int step=qMax(1,int(std::sqrt(im.width()*double(im.height())/18000)));
    for(int y=step/2;y<im.height();y+=step) {
        const auto *line=reinterpret_cast<const float *>(im.constScanLine(y));
        for(int x=step/2;x<im.width();x+=step) {
            const float *v=line+4*x;
            if(!std::isfinite(v[0]+v[1]+v[2]) || v[3]<.99f) continue;
            Pixel p; std::copy(v,v+3,p.rgb); describe(p);
            if(!includeExtremes && (p.y<.015 || p.y>.985)) continue;
            out.push_back(p);
        }
    }
    return out;
}
std::vector<Pixel> transformed(const std::vector<Pixel> &source,const oma_match_params &params,bool normaliseOnly=false) {
    std::vector<Pixel> out=source;
    for(auto &p:out) {
        float v[3]; if(normaliseOnly) oma_match_normalise(&params,p.rgb,v); else oma_match_colour(&params,p.rgb,v);
        std::copy(v,v+3,p.rgb); describe(p);
    }
    return out;
}
// Equal support per luminance cell, rather than per pixel. A bigger sky or
// a smaller grey card must not count as a change of exposure or saturation.
std::array<std::vector<Pixel>,7> support(const std::vector<Pixel> &v) {
    std::array<std::array<std::vector<Pixel>,64>,7> cells;
    for(const auto &p:v) cells[p.hue][qBound(0,int(p.y*64),63)].push_back(p);
    std::array<std::vector<Pixel>,7> result;
    for(int h=0;h<7;++h) for(auto &cell:cells[h]) if(cell.size()>=3) {
        Pixel p{};
        for(int c=0;c<3;++c) {std::vector<double> values;for(const auto &s:cell)values.push_back(s.rgb[c]);p.rgb[c]=median(values);}
        describe(p);result[h].push_back(p);
    }
    return result;
}
struct Pair { Pixel ref,target; double weight=1; };
std::vector<Pair> corresponding(const QImage &reference,const QImage &target) {
    if(std::abs(reference.width()/double(reference.height())-target.width()/double(target.height()))>.015)return {};
    constexpr int n=128;
    const QImage a=ColourPipeline::linear(reference).scaled(n,n,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    const QImage b=ColourPipeline::linear(target).scaled(n,n,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    std::vector<Pair> pairs;double dot=0,aa=0,bb=0,directions=0,directionWeight=0;
    int edges=0;
    for(int y=1;y<n-1;++y)for(int x=1;x<n-1;++x) {
        const auto *r=reinterpret_cast<const float *>(a.constScanLine(y))+4*x,*t=reinterpret_cast<const float *>(b.constScanLine(y))+4*x;
        if(r[3]<.99f || t[3]<.99f)continue;
        Pair pair;std::copy(r,r+3,pair.ref.rgb);std::copy(t,t+3,pair.target.rgb);describe(pair.ref);describe(pair.target);
        if(pair.ref.y<.025 || pair.ref.y>.975 || pair.target.y<.025 || pair.target.y>.975)continue;
        pairs.push_back(pair);
        // Correlate local edge locations/directions, not the global histogram.
        double da[2],db[2];
        for(int axis=0;axis<2;++axis) {
            const auto *rr=axis?reinterpret_cast<const float *>(a.constScanLine(y+1))+4*x:r+4;
            const auto *tt=axis?reinterpret_cast<const float *>(b.constScanLine(y+1))+4*x:t+4;
            da[axis]=oma_match_encode(qMax(0.f,oma_match_luma(rr)))-pair.ref.y;
            db[axis]=oma_match_encode(qMax(0.f,oma_match_luma(tt)))-pair.target.y;
            dot+=da[axis]*db[axis];aa+=da[axis]*da[axis];bb+=db[axis]*db[axis];
        }
        const double na=std::hypot(da[0],da[1]),nb=std::hypot(db[0],db[1]);
        if(na>.0005 && nb>.0005) {
            const double weight=qMin(.02,std::sqrt(na*nb));
            directions+=weight*(da[0]*db[0]+da[1]*db[1])/(na*nb);
            directionWeight+=weight;++edges;
        }
    }
    if(pairs.size()<512 || aa<.01 || bb<.01)return {};
    if(dot/std::sqrt(aa*bb)<.96) {
        // A monotone tone curve changes gradient magnitudes, not their
        // directions or pixel brightness ranks. Require BOTH to agree so
        // similar histograms or a shared horizon alone cannot imply alignment.
        if(edges<256 || directions/qMax(1e-9,directionWeight)<.985)return {};
        std::vector<size_t> order(pairs.size());
        std::vector<double> ranks[2];
        for(int side=0;side<2;++side) {
            for(size_t i=0;i<order.size();++i)order[i]=i;
            const auto value=[&](size_t i){return side?pairs[i].target.y:pairs[i].ref.y;};
            std::sort(order.begin(),order.end(),[&](size_t i,size_t j){return value(i)<value(j);});
            ranks[side].resize(order.size());
            for(size_t begin=0;begin<order.size();) {
                size_t end=begin+1;while(end<order.size() && value(order[end])==value(order[begin]))++end;
                for(size_t i=begin;i<end;++i)ranks[side][order[i]]=(begin+end-1)*.5;
                begin=end;
            }
        }
        double cross=0,ra=0,rb=0;const double mean=(pairs.size()-1)*.5;
        for(size_t i=0;i<pairs.size();++i){const double a=ranks[0][i]-mean,b=ranks[1][i]-mean;cross+=a*b;ra+=a*a;rb+=b*b;}
        if(cross/std::sqrt(qMax(1e-12,ra*rb))<.99)return {};
    }
    return pairs;
}
// Tiny regularised least-squares systems (at most seven free parameters).
// Fitting residuals makes identity an exact solution, including repeated tones.
std::array<double,7> solve(double a[7][8]) {
    for(int i=0;i<7;++i) {
        int pivot=i;for(int j=i+1;j<7;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
        for(int k=0;k<8;++k)std::swap(a[i][k],a[pivot][k]);
        if(std::abs(a[i][i])<1e-10)continue;
        const double d=a[i][i];for(int k=i;k<8;++k)a[i][k]/=d;
        for(int j=0;j<7;++j)if(j!=i){const double f=a[j][i];for(int k=i;k<8;++k)a[j][k]-=f*a[i][k];}
    }
    std::array<double,7> x{};for(int i=0;i<7;++i)x[i]=a[i][7];return x;
}
void equation(double a[7][8],const double *x,double residual,double weight) {
    for(int i=0;i<7;++i){for(int j=0;j<7;++j)a[i][j]+=weight*x[i]*x[j];a[i][7]+=weight*x[i]*residual;}
}
std::vector<Pair> supportedPairs(const std::vector<Pixel> &reference,const std::vector<Pixel> &target) {
    const auto r=support(reference),t=support(target);std::vector<Pair> pairs;
    for(int h=0;h<7;++h) if(!r[h].empty() && !t[h].empty()) {
        const int n=qMax(r[h].size(),t[h].size());
        for(int i=0;i<n;++i) {
            const auto &a=r[h][qMin(r[h].size()-1,size_t((i+.5)*r[h].size()/n))];
            const auto &b=t[h][qMin(t[h].size()-1,size_t((i+.5)*t[h].size()/n))];
            pairs.push_back({a,b,1./n});
        }
    }
    return pairs;
}
// With similar colour AND brightness coverage, density carries useful
// contrast information even when the framing differs. A different-sized sky
// or grey card fails these overlap checks and stays on balanced support.
bool similarCoverage(const std::vector<Pixel> &a,const std::vector<Pixel> &b) {
    double ah[7]={},bh[7]={},ay[4]={},by[4]={};
    for(const auto &p:a){ah[p.hue]+=1./a.size();ay[qBound(0,int(p.y*4),3)]+=1./a.size();}
    for(const auto &p:b){bh[p.hue]+=1./b.size();by[qBound(0,int(p.y*4),3)]+=1./b.size();}
    double hue=0,light=0;for(int i=0;i<7;++i)hue+=qMin(ah[i],bh[i]);for(int i=0;i<4;++i)light+=qMin(ay[i],by[i]);
    return hue>.85 && light>.9;
}
std::vector<Pair> distributionPairs(const std::vector<Pixel> &reference,const std::vector<Pixel> &target) {
    std::array<std::vector<Pixel>,7> a,b;for(const auto &p:reference)a[p.hue].push_back(p);for(const auto &p:target)b[p.hue].push_back(p);
    std::vector<Pair> pairs;
    for(int h=0;h<7;++h)if(a[h].size()>32 && b[h].size()>32) {
        const auto less=[](const Pixel &a,const Pixel &b){return a.y<b.y;};std::sort(a[h].begin(),a[h].end(),less);std::sort(b[h].begin(),b[h].end(),less);
        const double weight=7*qMin(a[h].size()/double(reference.size()),b[h].size()/double(target.size()))/128;
        // Average small quantile neighbourhoods to keep random grain from
        // being mistaken for a different palette at one rank.
        for(int i=0;i<128;++i) {
            const auto sample=[&](const std::vector<Pixel> &v){Pixel p{};for(int k=0;k<9;++k){const auto &s=v[qMin(v.size()-1,size_t((i+(k+.5)/9)*v.size()/128))];for(int c=0;c<3;++c)p.rgb[c]+=s.rgb[c]/9;}describe(p);return p;};
            pairs.push_back({sample(a[h]),sample(b[h]),weight});
        }
    }
    return pairs;
}
// Match the low-chroma part of each tonal rank, not whichever subject happens
// to occupy a shared 60-degree hue bin. A cyan atmosphere can have no truly
// grey pixels; its least saturated surfaces still describe the creative cast.
// Ranking by luminance lets a sunset's warm sky guide a darker sky without
// treating their different absolute brightness as an exposure error.
std::vector<Pair> palettePairs(std::vector<Pixel> reference,std::vector<Pixel> target) {
    const auto order=[](const Pixel &a,const Pixel &b){return a.y<b.y;};
    std::sort(reference.begin(),reference.end(),order);std::sort(target.begin(),target.end(),order);
    const auto sample=[](const std::vector<Pixel> &v,double rank,double fraction) {
        const int begin=qMax(0,int((rank-.12)*v.size())),end=qMin(int(v.size()),int((rank+.12)*v.size()));
        std::vector<Pixel> candidates(v.begin()+begin,v.begin()+end);
        std::sort(candidates.begin(),candidates.end(),[](const Pixel &a,const Pixel &b){
            return std::hypot(a.lab[1],a.lab[2])/qMax(.01f,a.lab[0])<std::hypot(b.lab[1],b.lab[2])/qMax(.01f,b.lab[0]);
        });
        candidates.resize(qMax(size_t(1),size_t(candidates.size()*fraction)));Pixel result{};
        for(int c=0;c<3;++c){std::vector<double> values;for(const auto &p:candidates)values.push_back(p.rgb[c]);result.rgb[c]=median(values);}
        describe(result);return result;
    };
    std::vector<Pair> result;
    for(int i=0;i<9;++i)result.push_back({sample(reference,.1+i*.1,.9),sample(target,.1+i*.1,.5),1.});
    return result;
}
QVariantMap settingsMap(const oma_match_params &p) {
    QVariantMap m;
#define FIELD(f) m[#f]=double(p.f)
    FIELD(exposure); FIELD(warmth); FIELD(tint); FIELD(exposure_on); FIELD(wb_on);
    FIELD(tone_strength); FIELD(colour_strength); FIELD(grain_strength); FIELD(colour_scale);
    FIELD(grain_amount); FIELD(grain_size); FIELD(grain_roughness); FIELD(grain_colour); FIELD(grain_texture);
    FIELD(grain_shadows); FIELD(grain_midtones); FIELD(grain_highlights); FIELD(protect_skin); FIELD(render_version);
    FIELD(tone_on); FIELD(colour_on); FIELD(grain_on);
#undef FIELD
    for(int i=0;i<9;++i) m[QString("tone[%1]").arg(i)]=double(p.tone[i]);
    for(int i=0;i<3;++i) { m[QString("colour_a[%1]").arg(i)]=double(p.colour_a[i]); m[QString("colour_b[%1]").arg(i)]=double(p.colour_b[i]); }
    return m;
}
}
Options Options::fromMap(const QVariantMap &m) {
    Options o; o.creative=m.value("mode").toString()=="creative";
    o.exposure=m.value("exposure",true).toBool(); o.whiteBalance=m.value("whiteBalance",true).toBool();
    o.tone=m.value("tone",true).toBool(); o.colour=m.value("colour",true).toBool(); o.grain=m.value("grain",false).toBool();
    o.protectSkin=m.value("protectSkin",true).toBool();
    const auto strength=[&](const char *s) { bool ok=false; double v=m.value(s,1).toDouble(&ok); return ok&&std::isfinite(v)?qBound(0.,v,1.):1.; };
    o.colourStrength=strength("colourStrength"); o.toneStrength=strength("toneStrength"); o.grainStrength=strength("grainStrength"); return o;
}
QVariantMap Options::toMap() const { return {{"mode",creative?"creative":"consistency"},{"exposure",exposure},{"whiteBalance",whiteBalance},{"tone",tone},{"colour",colour},{"grain",grain},{"protectSkin",protectSkin},{"colourStrength",colourStrength},{"toneStrength",toneStrength},{"grainStrength",grainStrength}}; }
QVariantMap Grain::report() const {
    return {{"amount",amount},{"size",size},{"roughness",roughness},{"colour",colour},{"confidence",confidence},{"samples",samples},
        {"shadows",zones[0]},{"midtones",zones[1]},{"highlights",zones[2]},{"compression",compression},{"textured",textured}};
}
QVariantMap Result::report() const { return {{"referenceGrain",referenceGrain.report()},{"targetGrain",targetGrain.report()},{"confidence",confidence},{"fit",fit},{"warnings",warnings},{"parameters",parameters(params)}}; }
QVariantMap parameters(const oma_match_params &p) { return settingsMap(p); }
bool parameters(const QVariantMap &map,oma_match_params *p) {
    if(!p) return false;
    oma_match_defaults(p);
    // Serialized controls predating v2 have the original deterministic grain.
    if(!map.contains("grain_texture"))p->grain_texture=0;
    if(!map.contains("render_version"))p->render_version=map.contains("grain_texture")?2:1;
    const auto assign=[&](const QString &key,float &field) { if(!map.contains(key)) return true; bool ok=false; const double d=map.value(key).toDouble(&ok); if(!ok || !std::isfinite(d)) return false; field=float(d); return true; };
#define FIELD(f) if(!assign(#f,p->f)) return false
    FIELD(exposure); FIELD(warmth); FIELD(tint); FIELD(exposure_on); FIELD(wb_on); FIELD(tone_strength); FIELD(colour_strength);
    FIELD(grain_strength); FIELD(colour_scale); FIELD(grain_amount); FIELD(grain_size); FIELD(grain_roughness); FIELD(grain_colour); FIELD(grain_texture);
    FIELD(grain_shadows); FIELD(grain_midtones); FIELD(grain_highlights); FIELD(protect_skin); FIELD(render_version);
    FIELD(tone_on); FIELD(colour_on); FIELD(grain_on);
#undef FIELD
    for(int i=0;i<9;++i) if(!assign(QString("tone[%1]").arg(i),p->tone[i])) return false;
    for(int i=0;i<3;++i) if(!assign(QString("colour_a[%1]").arg(i),p->colour_a[i]) || !assign(QString("colour_b[%1]").arg(i),p->colour_b[i])) return false;
    for(const auto &row:controls()) {
        const auto r=row.toMap(); const double value=parameters(*p).value(r.value("field").toString()).toDouble();
        if(value<r.value("uiMin").toDouble() || value>r.value("uiMax").toDouble()) return false;
    }
    // Automatic curves are monotone. Preserve deliberate manual curve edits
    // as entered, including when capturing a pre-match comparison.
    return true;
}
QVariantList controls() {
    QVariantList out;
    const auto add=[&](const QString &field,const QString &label,double lo,double hi,int decimals=2,double factor=1,const QString &suffix=QString()) {
        out<<QVariantMap{{"group","Color"},{"op","omarawmatch"},{"field",field},{"label",label},{"uiMin",lo},{"uiMax",hi},{"decimals",decimals},{"factor",factor},{"suffix",suffix}};
    };
    add("exposure","Match exposure",-3,3,2,1," EV"); add("warmth","Match warmth",-1,1,1,100); add("tint","Match tint",-1,1,1,100);
    add("exposure_on","Match exposure enabled",0,1); add("wb_on","Match white balance enabled",0,1);
    add("tone_on","Match tone enabled",0,1); add("colour_on","Match colour enabled",0,1); add("grain_on","Match grain enabled",0,1);
    add("tone_strength","Tone strength",0,1,0,100," %"); add("colour_strength","Colour strength",0,1,0,100," %"); add("grain_strength","Grain strength",0,1,0,100," %");
    for(int i=0;i<9;++i) add(QString("tone[%1]").arg(i),QString("Tone at %1%").arg(i*12.5),-.4,.4,2,100);
    for(int i=0;i<3;++i) {
        const QString zone=QStringList{"Shadows","Midtones","Highlights"}[i];
        add(QString("colour_a[%1]").arg(i),zone+" green–magenta",-.1,.1,2,100);
        add(QString("colour_b[%1]").arg(i),zone+" blue–yellow",-.1,.1,2,100);
    }
    add("colour_scale","Match saturation",0,2,0,100," %");
    add("grain_amount","Grain amount",0,32,2); add("grain_size","Grain size (at 3000 px)",.2,12,2,1," px");
    add("grain_roughness","Grain roughness",0,1,0,100," %"); add("grain_colour","Coloured grain",0,1,0,100," %");
    add("grain_texture","Organic grain",0,1,0);
    for(const char *zone:{"shadows","midtones","highlights"}) add(QString("grain_")+zone,QString("Grain in ")+zone,0,2,0,100," %");
    add("protect_skin","Protect skin-like colours",0,1,0,100," %");
    add("render_version","Match rendering version",1,5,0);
    auto version=out.last().toMap();version["internal"]=true;out.last()=version;
    return out;
}
Source fromImage(const QImage &image) {
    Source s; s.fullSize=image.size(); if(image.isNull()) return s;
    s.overview=ColourPipeline::linear((qMax(image.width(),image.height())>1600?image.scaled(1600,1600,Qt::KeepAspectRatio,Qt::SmoothTransformation):image));
    const int side=qMin(192,qMin(image.width(),image.height()));
    for(int y=0;y<3;++y) for(int x=0;x<3;++x) {
        const int ox=(image.width()-side)*x/2,oy=(image.height()-side)*y/2;
        s.grainTiles<<ColourPipeline::linear(image.copy(ox,oy,side,side));
    }
    return s;
}
Grain grain(const Source &source,const std::atomic<int> *cancel,int ticket) {
    Grain out;
    struct Patch { double sigma,size,colour,rough,mean,texture; int zone; };
    std::vector<Patch> accepted;
    int blocked=0,structured=0,total=0;
    constexpr int n=24; constexpr double centre=(n-1)/2.;
    for(const auto &tile:source.grainTiles) {
        if(stopped(cancel,ticket)) return {};
        const QImage im=ColourPipeline::srgb(tile);
        for(int oy=0;oy+n<=im.height();oy+=n) for(int ox=0;ox+n<=im.width();ox+=n) {
            ++total;
            double mean[3]={},dx[3]={},dy[3]={},den=0,values[n*n][3];
            for(int y=0;y<n;++y) {
                const auto *line=reinterpret_cast<const float *>(im.constScanLine(oy+y));
                for(int x=0;x<n;++x) {
                    const int i=y*n+x; den+=(x-centre)*(x-centre);
                    for(int c=0;c<3;++c) { const double v=line[(ox+x)*4+c]*255.; values[i][c]=v; mean[c]+=v; dx[c]+=v*(x-centre); dy[c]+=v*(y-centre); }
                }
            }
            for(int c=0;c<3;++c) {mean[c]/=n*n;dx[c]/=den;dy[c]/=den;}
            const double light=(mean[0]+mean[1]+mean[2])/3.;
            if(light<12 || light>243 || !std::isfinite(light)) continue;
            double residual[n*n][3],lum[n*n],variance[3]={},cov=0,v2=0,v4=0;
            for(int y=0;y<n;++y) for(int x=0;x<n;++x) {
                const int i=y*n+x;
                for(int c=0;c<3;++c) {const double v=values[i][c]-mean[c]-dx[c]*(x-centre)-dy[c]*(y-centre);residual[i][c]=v;variance[c]+=v*v;}
                lum[i]=(residual[i][0]+residual[i][1]+residual[i][2])/3.;v2+=lum[i]*lum[i];v4+=std::pow(lum[i],4);
                cov+=residual[i][0]*residual[i][1]+residual[i][0]*residual[i][2]+residual[i][1]*residual[i][2];
            }
            for(double &v:variance) v/=n*n;
            const double sigma=std::sqrt((variance[0]+variance[1]+variance[2])/3.);
            if(!std::isfinite(sigma)) continue;
            double xx=0,yy=0,xy=0,cor[4]={},power=0,phase[8]={}; int phaseN[8]={};
            for(int y=1;y<n-4;++y) for(int x=1;x<n-4;++x) {
                const int i=y*n+x; const double gx=lum[i+1]-lum[i],gy=lum[i+n]-lum[i];
                xx+=gx*gx;yy+=gy*gy;xy+=gx*gy;power+=lum[i]*lum[i];
                phase[x%8]+=gx*gx;phaseN[x%8]++; phase[y%8]+=gy*gy;phaseN[y%8]++;
                for(int k=0;k<4;++k) cor[k]+=.5*lum[i]*(lum[i+k+1]+lum[i+(k+1)*n]);
            }
            for(double &c:cor) c/=qMax(1e-10,power);
            std::vector<double> phases;for(int k=0;k<8;++k) phases.push_back(phase[k]/qMax(1,phaseN[k]));
            const double anisotropy=std::hypot(xx-yy,2*xy)/(xx+yy+1e-9);
            const bool blocks=*std::max_element(phases.begin(),phases.end())>qMax(4.,median(phases)*4.5);
            const double slope=std::sqrt(dx[0]*dx[0]+dy[0]*dy[0]+dx[1]*dx[1]+dy[1]*dy[1]+dx[2]*dx[2]+dy[2]*dy[2]);
            const bool texture=sigma>.5 && (anisotropy>.58 || cor[0]<-.24 || cor[3]>.5 || cor[2]>cor[0]+.22 || slope>1.4);
            if(blocks) {++blocked;continue;} if(texture) {++structured;continue;}
            const double correlation=cov/(n*n*(std::sqrt(variance[0]*variance[1])+std::sqrt(variance[0]*variance[2])+std::sqrt(variance[1]*variance[2]))+1e-9);
            const double size=cor[0]>.07?std::sqrt(-.5/std::log(qBound(.07,cor[0],.9)))*1.7:.65;
            const double kurtosis=v4*(n*n)/(v2*v2+1e-9);
            accepted.push_back({sigma,size,qBound(0.,1-correlation,1.),qBound(0.,(kurtosis-2.4)/2.,1.),light/255.,slope+sigma*.025,light<85?0:light>170?2:1});
        }
    }
    out.compression=blocked>qMax(3,total/12); out.textured=structured>total/2;
    if(accepted.size()<6 || qMax(source.fullSize.width(),source.fullSize.height())<256) return out;
    // The quietest candidates are the defensible noise floor. Do not turn
    // bark, fabric, hair or repeated sharpening halos into an extra effect.
    std::array<std::vector<Patch>,3> candidates;
    for(const auto &patch:accepted)candidates[patch.zone].push_back(patch);
    accepted.clear();
    // Keep quiet samples in EACH tonal range; a clean sky must not discard
    // noisier shadow samples and silently erase their distribution estimate.
    for(auto &zone:candidates) {
        std::sort(zone.begin(),zone.end(),[](const Patch &a,const Patch &b){return a.texture<b.texture;});
        if(zone.size()>=6)zone.resize(qMax(size_t(6),zone.size()*2/5));
        accepted.insert(accepted.end(),zone.begin(),zone.end());
    }
    out.samples=int(accepted.size());
    std::vector<double> sizes,colours,roughness; std::array<std::vector<double>,3> zones;
    for(const auto &p:accepted) {sizes.push_back(p.size);colours.push_back(p.colour);roughness.push_back(p.rough);zones[p.zone].push_back(p.sigma);}
    out.size=qBound(.2,median(sizes)*3000./qMax(source.fullSize.width(),source.fullSize.height()),12.);
    out.roughness=median(roughness);out.colour=median(colours);
    double variance=0;int count=0;
    for(int z=0;z<3;++z) {
        out.zones[z]=zones[z].size()>=3?quantile(zones[z],.35):-1;
        if(out.zones[z]>=0) {if(out.zones[z]<.8)out.zones[z]=0;variance+=out.zones[z]*out.zones[z];++count;}
    }
    out.amount=count?std::sqrt(variance/count):0;
    out.confidence=qMin(1.,accepted.size()/40.)*(out.compression?.4:1.)*(out.textured?.7:1.);
    if(out.confidence<.25) {out.amount=0;for(double &z:out.zones) if(z>=0) z=0;}
    if(out.amount==0) {out.size=1;out.roughness=.5;out.colour=0;}
    return out;
}
Result analyse(const Source &reference,const Source &target,const Options &o,const std::atomic<int> *cancel,int ticket) {
    Result r; auto &p=r.params;
    p.exposure_on=o.exposure; p.wb_on=o.whiteBalance;
    p.tone_on=o.tone;p.colour_on=o.colour;p.grain_on=o.grain;
    p.protect_skin=o.protectSkin;
    p.tone_strength=o.toneStrength;p.colour_strength=o.colourStrength;p.grain_strength=o.grainStrength;
    const auto ref=pixels(reference.overview),input=pixels(target.overview);
    if(ref.size()<64 || input.size()<64) {r.warnings<<"Not enough unclipped image content to estimate a reliable match.";return r;}
    if(stopped(cancel,ticket)) return r;
    auto pairs=corresponding(reference.overview,target.overview);
    const bool aligned=!pairs.empty();
    if(!aligned)pairs=supportedPairs(ref,input);
    std::array<std::vector<double>,2> wb;
    for(const auto &pair:pairs) {
        const auto &a=pair.ref,&b=pair.target;
        if(std::min({a.rgb[0],a.rgb[1],a.rgb[2],b.rgb[0],b.rgb[1],b.rgb[2]})<.004)continue;
        if(!aligned && (std::hypot(a.lab[1],a.lab[2])/a.lab[0]>.045 || std::hypot(b.lab[1],b.lab[2])/b.lab[0]>.045))continue;
        wb[0].push_back(std::log2(a.rgb[0]/a.rgb[2])-std::log2(b.rgb[0]/b.rgb[2]));
        wb[1].push_back((std::log2(std::sqrt(a.rgb[0]*a.rgb[2])/a.rgb[1])-std::log2(std::sqrt(b.rgb[0]*b.rgb[2])/b.rgb[1]))/.75);
    }
    if(wb[0].size()>=8) {
        const double bound=aligned?.8:.3;
        p.warmth=qBound(-bound,median(wb[0]),bound);p.tint=qBound(-bound,median(wb[1]),bound);
    } else if(o.whiteBalance)r.warnings<<"Few comparable neutral areas: white balance is left unchanged.";
    const auto guardInput=pixels(target.overview,true);
    // Include bright whites omitted from estimation. Otherwise a neutral
    // highlight outside the fitting range could clip after the WB correction.
    for(int attempt=0;attempt<16 && !aligned;++attempt) {
        int clipped=0;
        for(const auto &v:guardInput){float rgb[3];oma_match_normalise(&p,v.rgb,rgb);if(std::max({v.rgb[0],v.rgb[1],v.rgb[2]})<=1 && std::max({rgb[0],rgb[1],rgb[2]})>1.0001f)++clipped;}
        if(clipped<=int(guardInput.size()/1000))break;
        p.warmth*=.5;p.tint*=.5;
    }
    auto normalised=transformed(input,p,true);
    if(!aligned)pairs=supportedPairs(ref,normalised);
    else for(auto &pair:pairs){float v[3];oma_match_normalise(&p,pair.target.rgb,v);std::copy(v,v+3,pair.target.rgb);describe(pair.target);}
    std::vector<double> shifts;
    for(const auto &pair:pairs)shifts.push_back(std::log2(qMax(.0001f,oma_match_luma(pair.ref.rgb))/qMax(.0001f,oma_match_luma(pair.target.rgb))));
    const double spread=quantile(shifts,.8)-quantile(shifts,.2);
    // Overall confidence is shared colour support, as used by the service's
    // preview eligibility check. Zero exposure reliability must not reject a
    // usable conservative colour/grain preview of a different scene.
    double rh[7]={},th[7]={},overlap=0;
    for(const auto &v:ref)rh[v.hue]+=1./ref.size();
    for(const auto &v:normalised)th[v.hue]+=1./normalised.size();
    for(int h=0;h<7;++h)overlap+=qMin(rh[h],th[h]);
    r.confidence=aligned?1:pairs.empty()?0:overlap;
    // For unrelated frames brightness and material reflectance are ambiguous.
    // Disagreement between shared colours reduces correction, not saturation.
    const double reliability=aligned?1:qBound(0.,1-spread/1.5,1.);
    p.exposure=qBound(aligned?-2.:-.65,median(shifts),aligned?2.:.65)*reliability;
    if(!aligned)r.warnings<<(o.creative
        ? "Different composition: transferring the reference's tone and palette. Exposure and white balance remain conservative."
        : "Different composition: exposure, contrast and colour are conservative. Use Creative Look for a stronger transfer.");
    if(!aligned && p.exposure_on && p.exposure>0) {
        std::vector<double> highlights;for(const auto &v:transformed(guardInput,p,true))highlights.push_back(std::max({v.rgb[0],v.rgb[1],v.rgb[2]}));
        const double high=quantile(highlights,.999)/std::exp2(p.exposure);
        if(high>0)p.exposure=qMax(0.,qMin(double(p.exposure),std::log2(1./high)));
    }
    normalised=transformed(input,p,true);
    // Rebuild pairs after normalisation, always from the original target.
    if(aligned){pairs=corresponding(reference.overview,target.overview);for(auto &pair:pairs){float v[3];oma_match_normalise(&p,pair.target.rgb,v);std::copy(v,v+3,pair.target.rgb);describe(pair.target);}}
    else pairs=supportedPairs(ref,normalised);
    const bool similar=!aligned && similarCoverage(ref,normalised);
    if(similar)pairs=distributionPairs(ref,normalised);
    double toneSystem[7][8]={};
    const double count=aligned?pairs.size():7.;
    for(int i=0;i<7;++i)toneSystem[i][i]=count*(aligned?.0001:.03);
    for(const auto &pair:pairs) {
        double basis[7]={};const double t=qBound(0.,double(pair.target.y),1.)*8;const int i=qMin(7,int(t));
        if(i>0)basis[i-1]=1-(t-i);
        if(i<7)basis[i]=t-i;
        equation(toneSystem,basis,pair.ref.y-pair.target.y,pair.weight);
    }
    // Smooth unsupported knots, with fixed black/white endpoints. No
    // extrapolated grey pedestal or invented highlight lift.
    for(int i=0;i<7;++i){double x[7]={};x[i]=2;if(i>0)x[i-1]=-1;if(i<6)x[i+1]=-1;equation(toneSystem,x,0,count*.002);}
    const auto curve=solve(toneSystem);const double limit=aligned?.18:(o.creative?.12:.04);
    // Exposure uncertainty between scenes must not disable an explicitly
    // requested creative contrast curve. Equal material support still avoids
    // treating a larger area of an unchanged colour as a new look.
    for(int i=1;i<8;++i)p.tone[i]=qBound(-limit,curve[i-1],limit)*((aligned || similar || o.creative)?1:reliability);
    for(int i=1;i<9;++i)p.tone[i]=qMax(p.tone[i],p.tone[i-1]-.10f);
    for(int i=7;i>=0;--i)p.tone[i]=qMin(p.tone[i],p.tone[i+1]+.10f);
    // Constrain only the two knots affecting an endangered highlight. The
    // previous global attenuation erased a useful shadow lift because a few
    // bright pixels had no headroom. These are constraints on the full-strength
    // estimate, so a later strength change cannot invalidate the protection.
    const auto guardNormalised=transformed(guardInput,p,true);
    for(int attempt=0;attempt<8;++attempt) {
        bool changed=false;
        for(const auto &v:guardNormalised) {
            const float peak=std::max({v.rgb[0],v.rgb[1],v.rgb[2]}),l=oma_match_luma(v.rgb);
            if(peak<=0 || peak>1 || l<=1e-7f)continue;
            const float t=qBound(0.f,v.y,1.f)*8;const int i=qMin(7,int(t));
            const float w[2]={1-(t-i),t-i};
            const float ceiling=qMax(0.f,oma_match_encode(l/peak)-v.y);
            const float excess=w[0]*p.tone[i]+w[1]*p.tone[i+1]-ceiling;
            if(excess<=1e-6f)continue;
            float norm=0;for(int j=0;j<2;++j)if(p.tone[i+j]>0)norm+=w[j]*w[j];
            if(norm<=1e-9f)continue;
            for(int j=0;j<2;++j)if(p.tone[i+j]>0)p.tone[i+j]=qMax(0.f,p.tone[i+j]-excess*w[j]/norm);
            changed=true;
        }
        // Lower preceding knots if necessary to retain a monotone curve;
        // lowering cannot introduce a new highlight excursion.
        for(int i=7;i>=0;--i)p.tone[i]=qMin(p.tone[i],p.tone[i+1]+.10f);
        if(!changed)break;
    }
    // Jointly fit chroma scale and split-tone offsets, with equal weight for
    // shared materials. Fitting offsets before saturation double-corrected
    // colours and let a grey background drain a colourful target.
    double colourSystem[7][8]={};
    colourSystem[0][0]=count*(aligned?.000001:.0005);
    for(int i=1;i<7;++i)colourSystem[i][i]=count*(aligned?.0001:.05);
    oma_match_params toneOnly=p;toneOnly.exposure_on=toneOnly.wb_on=0;
    for(const auto &pair:pairs) {
        float rgb[3],lab[3],weights[3];oma_match_colour(&toneOnly,pair.target.rgb,rgb);oma_match_oklab(rgb,lab);oma_match_weights(pair.target.y,weights);
        for(int c=0;c<2;++c){double x[7]={};x[0]=lab[c+1];for(int z=0;z<3;++z)x[1+c*3+z]=weights[z];equation(colourSystem,x,pair.ref.lab[c+1]-lab[c+1],pair.weight);}
    }
    const auto colour=solve(colourSystem);
    // Actual pixel correspondence is evidence for strong desaturation too.
    // Retain conservative limits when subjects cannot be compared directly.
    p.colour_scale=qBound(aligned?0.:o.creative?.65:.85,1+colour[0],aligned?2.:o.creative?1.5:1.2);
    for(int z=0;z<3;++z){p.colour_a[z]=qBound(-.035,colour[1+z],.035);p.colour_b[z]=qBound(-.035,colour[4+z],.035);}
    if(!aligned && !similar) {
        // Existing identical colours at different area coverage are not a
        // new look. Keep the support-invariant identity case unchanged.
        double difference=0,weight=0;
        for(const auto &pair:pairs){difference+=pair.weight*std::hypot(pair.ref.lab[1]-pair.target.lab[1],pair.ref.lab[2]-pair.target.lab[2]);weight+=pair.weight;}
        const double lookConfidence=qBound(0.,(difference/qMax(.001,weight)-.002)/.012,1.);
        if(o.creative && lookConfidence>0) {
            // Keep saturation tied to shared colours; fitting it jointly with
            // an unrelated scene's cast can erase the target's own colours.
            p.colour_scale=qBound(.85f,p.colour_scale,1.3f);
            double system[7][8]={};for(int k=0;k<7;++k)system[k][k]=.12;
            for(int c=0;c<2;++c)for(int z=0;z<2;++z){double x[7]={};x[c*3+z]=1;x[c*3+z+1]=-1;equation(system,x,0,.8);}
            for(const auto &pair:palettePairs(ref,normalised)) {
                float w[3],rgb[3],lab[3];oma_match_weights(pair.target.y,w);
                oma_match_colour(&toneOnly,pair.target.rgb,rgb);oma_match_oklab(rgb,lab);
                const double chroma=std::hypot(pair.ref.lab[1],pair.ref.lab[2])/qMax(.01f,pair.ref.lab[0]);
                const double trust=qBound(.1,1-(chroma-.2)/.3,1.);
                for(int c=0;c<2;++c){double x[7]={};for(int z=0;z<3;++z)x[c*3+z]=w[z];equation(system,x,pair.ref.lab[c+1]*lab[0]/qMax(.1f,pair.ref.lab[0])-lab[c+1]*p.colour_scale,trust);}
            }
            const auto palette=solve(system);
            for(int z=0;z<3;++z){p.colour_a[z]=qBound(-.05,palette[z],.05)*lookConfidence;p.colour_b[z]=qBound(-.05,palette[3+z],.05)*lookConfidence;}
        } else if(!o.creative) {
            // Unrelated subjects are weak evidence for a colour correction
            // in Consistency mode; strong palette transfer belongs to Look.
            for(int z=0;z<3;++z){p.colour_a[z]*=.25;p.colour_b[z]*=.25;}
            p.colour_scale=1+(p.colour_scale-1)*.4;
        }
    }
    // A near-monochrome JPEG can have a small tint and chroma quantisation,
    // especially near black. Relative C/L exaggerates these errors. Require
    // EVERY opaque overview pixel to have a small encoded RGB spread instead
    // of classifying only the median or sparsely sampled pixels. Small coloured
    // subjects still prevent an automatic monochrome conversion.
    double referenceSpread=0;
    if(o.creative) {
        const QImage encoded=ColourPipeline::srgb(reference.overview);
        for(int y=0;y<encoded.height();++y)for(int x=0;x<encoded.width();++x) {
            const auto *v=reinterpret_cast<const float *>(encoded.constScanLine(y))+4*x;
            if(v[3]<.99f)continue;
            if(!std::isfinite(v[0]+v[1]+v[2])){referenceSpread=1;continue;}
            referenceSpread=qMax(referenceSpread,double(std::max({v[0],v[1],v[2]})-std::min({v[0],v[1],v[2]})));
        }
    }
    const bool monochrome=o.creative && referenceSpread<.03;
    // Aligned photographs have direct evidence for a small intentional tint;
    // keep that fitted colour, including an exact self-match. Truly neutral
    // references can still explicitly remove colour from an aligned target.
    if(monochrome && (!aligned || referenceSpread<.0001)) {
        p.colour_scale=0;for(int z=0;z<3;++z)p.colour_a[z]=p.colour_b[z]=0;
    }
    // Suppress numerical residuals so re-matching identical images is exact.
    if(std::abs(p.exposure)<1e-5)p.exposure=0;
    if(std::abs(p.warmth)<1e-5)p.warmth=0;
    if(std::abs(p.tint)<1e-5)p.tint=0;
    if(std::abs(p.colour_scale-1)<1e-5)p.colour_scale=1;
    for(float &v:p.tone)if(std::abs(v)<1e-5)v=0;
    for(float &v:p.colour_a)if(std::abs(v)<1e-5)v=0;
    for(float &v:p.colour_b)if(std::abs(v)<1e-5)v=0;
    // Report residuals separately from shared-colour confidence. Only spatial
    // pairs measure recovery of the same photograph; shared-colour residuals
    // are a fitting diagnostic, never a percentage of visual similarity.
    double beforeError=0,afterError=0,weight=0;
    oma_match_params fitted=p;fitted.exposure_on=fitted.wb_on=0;
    for(const auto &pair:pairs) {
        float rgb[3],lab[3];oma_match_colour(&fitted,pair.target.rgb,rgb);oma_match_oklab(rgb,lab);
        for(int c=0;c<3;++c) {
            beforeError+=pair.weight*std::pow(pair.ref.lab[c]-pair.target.lab[c],2);
            afterError+=pair.weight*std::pow(pair.ref.lab[c]-lab[c],2);
        }
        weight+=pair.weight;
    }
    beforeError=std::sqrt(beforeError/qMax(1e-9,weight));afterError=std::sqrt(afterError/qMax(1e-9,weight));
    r.fit={{"samePhoto",aligned},{"basis",aligned?"pairedPixels":"sharedColours"},
           {"oklabRmsBeforeToneColour",beforeError},{"oklabRmsAfterToneColour",afterError},
           {"monochromeReference",monochrome}};
    if(aligned && o.colour && afterError>.01 && afterError>beforeError*.65)
        r.warnings<<"Some reference colours could not be reproduced with the current match controls. Compare the result before applying.";
    size_t newlyClipped=0;
    for(const auto &pixel:input) {
        float output[3];oma_match_colour(&p,pixel.rgb,output);
        bool before=false,after=false;
        for(int c=0;c<3;++c){before|=pixel.rgb[c]>1 || pixel.rgb[c]<0;after|=output[c]>1.0001f || output[c]<-.0001f;}
        newlyClipped+=after&&!before;
    }
    if(newlyClipped>input.size()/200) r.warnings<<"The match pushes some colours beyond the output range. Check highlight detail and reduce tone strength or the saved exposure correction.";
    if(stopped(cancel,ticket)) return r;
    // Measure the target after its proposed tone/colour correction: boosting
    // shadows also boosts existing noise. Only the remaining variance is added.
    r.referenceGrain=grain(reference,cancel,ticket);
    Source corrected=target;corrected.grainTiles.clear();
    for(const auto &tile:target.grainTiles) corrected.grainTiles<<render(tile,p,target.fullSize,{},1,cancel,ticket);
    r.targetGrain=grain(corrected,cancel,ticket);
    const auto &a=r.referenceGrain,&b=r.targetGrain;
    p.grain_size=a.size;p.grain_roughness=a.roughness;p.grain_colour=a.colour;
    if(o.grain) {
        if(a.confidence<.4 || b.confidence<.25) r.warnings<<"Grain could not be separated reliably from texture or compression. No automatic grain was added; its controls remain available.";
        else if(a.amount<=0) {
            r.warnings<<"No detectable grain in the reference. No grain was added.";
            if(b.amount>0) r.warnings<<"The target is already grainier. Reducing that needs a separate denoise step; no detail was removed.";
        }
        else {
            double extra[3];bool grainier=false;
            for(int z=0;z<3;++z) {const double as=a.zones[z]>=0?a.zones[z]:a.amount,bs=b.zones[z]>=0?b.zones[z]:b.amount;extra[z]=std::sqrt(qMax(0.,as*as-bs*bs));grainier|=bs>as*1.12+.3;}
            p.grain_amount=qMin(32.,std::max({extra[0],extra[1],extra[2]}));
            if(p.grain_amount>0) {p.grain_shadows=extra[0]/p.grain_amount;p.grain_midtones=extra[1]/p.grain_amount;p.grain_highlights=extra[2]/p.grain_amount;}
            if(grainier) r.warnings<<"The target is already grainier in some tones. Reducing that needs a separate denoise step; no detail was removed.";
        }
        if(a.compression || b.compression) r.warnings<<"Compression patterns were detected and excluded from grain samples.";
    }
    return r;
}
QImage render(const QImage &input,const oma_match_params &p,const QSize &fullSize,const QPoint &origin,double scale,const std::atomic<int> *cancel,int ticket) {
    QImage image=ColourPipeline::linear(input);image.detach();if(image.isNull()) return {};
    if(scale<=0) scale=qMin(image.width()/double(qMax(1,fullSize.width())),image.height()/double(qMax(1,fullSize.height())));
    const float edge=qMax(fullSize.width(),fullSize.height());
    for(int y=0;y<image.height();++y) {
        if(stopped(cancel,ticket)) return {};
        auto *line=reinterpret_cast<float *>(image.scanLine(y));
        for(int x=0;x<image.width();++x) {float v[3];oma_match_colour(&p,line+4*x,v);oma_match_grain(&p,v,origin.x()+(x+.5)/scale,origin.y()+(y+.5)/scale,scale,edge);std::copy(v,v+3,line+4*x);}
    }
    return image;
}
}
