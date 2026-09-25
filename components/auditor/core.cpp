#include "auditor.hpp"
#include <algorithm>
#include <cmath>

namespace auditor {
const char *name(Observation v) {
    switch(v) { case Observation::Present: return "present"; case Observation::Empty: return "empty"; default: return "unknown"; }
}
const char *name(State v) {
    switch(v) { case State::Idle:return "idle"; case State::Armed:return "armed";
        case State::Monitoring:return "monitoring"; case State::Removed:return "removed";
        case State::Alert:return "alert"; default:return "fault"; }
}
bool same_roi(const Roi &a, const Roi &b) { return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h; }
bool valid(const Settings &s) {
    return s.roi.x>=0 && s.roi.y>=0 && s.roi.w>=32 && s.roi.h>=32 &&
        s.roi.w<=W && s.roi.h<=H && s.roi.x<=W-s.roi.w && s.roi.y<=H-s.roi.h &&
        s.roi.w*s.roi.h<=W*H*3/4 && s.hour>=0 && s.hour<24 && s.minute>=0 && s.minute<60 &&
        s.interval_s>=1 && s.interval_s<=3600 && s.confirmations>=2 && s.confirmations<=10 &&
        s.window_s>=s.interval_s*s.confirmations && s.window_s<=86400 &&
        std::isfinite(s.max_distance) && s.max_distance>=1 && s.max_distance<=80 &&
        std::isfinite(s.margin) && s.margin>=1 && s.margin<=40 &&
        std::isfinite(s.context_distance) && s.context_distance>=1 && s.context_distance<=80 &&
        std::isfinite(s.stability) && s.stability>=1 && s.stability<=40 && s.profile>=0 && s.profile<=2;
}
float distance(const uint8_t *a, const uint8_t *b, size_t n) {
    if (!n) return 255;
    uint32_t sum=0;
    for(size_t i=0;i<n;++i) sum+=std::abs(int(a[i])-int(b[i]));
    return float(sum)/float(n);
}
float context_distance(const Sample &a, const Sample &b) {
    if(a.context_count<32 || a.context_count>GUARD || a.context_count!=b.context_count) return 255;
    return distance(a.context.data(),b.context.data(),a.context_count);
}
bool extract(const uint8_t *gray, size_t len, const Roi &r, Sample &out) {
    Settings s; s.roi=r;
    if(!gray || len!=W*H || !valid(s)) return false;
    unsigned dark=0, bright=0;
    for(int j=0;j<SIDE;++j) for(int i=0;i<SIDE;++i) {
        int x0=r.x+i*r.w/SIDE, x1=r.x+(i+1)*r.w/SIDE;
        int y0=r.y+j*r.h/SIDE, y1=r.y+(j+1)*r.h/SIDE;
        unsigned sum=0, count=0;
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) { sum+=gray[y*W+x]; ++count; }
        auto v=uint8_t(sum/count); out.roi[j*SIDE+i]=v;
        dark+=v<8; bright+=v>247;
    }
    // Pack only points outside ROI: a large crop must not dilute scene changes.
    int k=0;
    out.context.fill(0);
    for(int y=5;y<H;y+=10) for(int x=5;x<W;x+=10) {
        bool in=x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
        if(!in) out.context[k++]=gray[y*W+x];
    }
    out.context_count=uint16_t(k);
    out.quality=dark<N*0.7f && bright<N*0.7f && k>=32;
    return true;
}
bool calibrated(const Calibration &c, const Settings &s) {
    return valid(s) && c.has_present && c.has_empty && c.present.quality && c.empty.quality &&
        distance(c.present.roi.data(),c.empty.roi.data(),N)>2*s.margin &&
        context_distance(c.present,c.empty)<=s.stability;
}
Result classify(const Sample &v, const Calibration &c, const Settings &s) {
    Result r;
    if(!v.quality || !calibrated(c,s)) return r;
    r.present=distance(v.roi.data(),c.present.roi.data(),N);
    r.empty=distance(v.roi.data(),c.empty.roi.data(),N);
    r.context=std::min(context_distance(v,c.present),context_distance(v,c.empty));
    float separation=distance(c.present.roi.data(),c.empty.roi.data(),N);
    float radius=std::min(s.max_distance,separation*0.45f);
    if(r.context>s.context_distance || std::abs(r.present-r.empty)<s.margin) return r;
    if(r.present<=radius && r.present<r.empty) r.label=Observation::Present;
    if(r.empty<=radius && r.empty<r.present) r.label=Observation::Empty;
    return r;
}
bool Cycle::arm(int64_t now, int64_t due, Observation initial, const Settings &s) {
    if(active() || !valid(s) || due<now || initial!=Observation::Present) return false;
    settings_=s; due_=due; end_=due+int64_t(s.window_s)*1000; next_=due;
    last_=-1; streak_=0; latest_=Observation::Unknown; acknowledged=false; state=State::Armed;
    return true;
}
void Cycle::cancel() { state=State::Idle; acknowledged=false; streak_=0; last_=-1; }
bool Cycle::needs_sample(int64_t now) const { return active() && now>=next_ && now<=end_; }
void Cycle::observe(int64_t now, Observation value) {
    if(!active() || now<due_) return;
    if(now>end_) { tick(now); return; } // Never credit late captures.
    state=State::Monitoring;
    if(last_>=0 && now-last_>int64_t(settings_.interval_s)*1000+2000) streak_=0;
    last_=now; latest_=value;
    // Schedule from completion: no rapid catch-up frames after a slow camera.
    next_=now+int64_t(settings_.interval_s)*1000;
    streak_=value==Observation::Empty?streak_+1:0;
    if(streak_>=settings_.confirmations) state=State::Removed;
}
void Cycle::tick(int64_t now) {
    if(!active()) return;
    if(now>=due_) state=State::Monitoring;
    if(now>=end_) {
        bool fresh=last_>=due_ && now-last_<=int64_t(settings_.interval_s)*1000+2000;
        state=fresh && latest_==Observation::Present?State::Alert:State::Fault;
    }
}
void Cycle::acknowledge() { if(state==State::Alert || state==State::Fault) acknowledged=true; }
} // namespace auditor
