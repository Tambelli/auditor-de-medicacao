#include "auditor.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
using namespace auditor;
static int checks=0;
#define CHECK(expr) do { ++checks; if(!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); return 1; } } while(0)

int main() {
    Settings s;
    CHECK(valid(s));
    Settings bad=s; bad.roi.w=0; CHECK(!valid(bad));
    bad=s; bad.roi.x=150; CHECK(!valid(bad));
    bad=s; bad.roi={0,0,W,H}; CHECK(!valid(bad));
    bad=s; bad.interval_s=0; CHECK(!valid(bad));
    bad=s; bad.confirmations=1; CHECK(!valid(bad));
    bad=s; bad.confirmations=100000; bad.interval_s=100000; CHECK(!valid(bad));
    bad=s; bad.window_s=5; CHECK(!valid(bad));
    bad=s; bad.margin=std::numeric_limits<float>::quiet_NaN(); CHECK(!valid(bad));
    bad=s; bad.max_distance=std::numeric_limits<float>::infinity(); CHECK(!valid(bad));
    bad=s; bad.hour=24; CHECK(!valid(bad));
    std::array<uint8_t,W*H> img{};
    img.fill(100);
    Sample empty,present;
    CHECK(extract(img.data(),img.size(),s.roi,empty)); CHECK(empty.quality);
    CHECK(!extract(img.data(),img.size()-1,s.roi,empty));
    CHECK(!extract(nullptr,img.size(),s.roi,empty));
    // Synthetic object occupies most of the test compartment, no scene change.
    for(int y=s.roi.y;y<s.roi.y+s.roi.h;++y)
        for(int x=s.roi.x;x<s.roi.x+s.roi.w;++x) img[y*W+x]=180;
    CHECK(extract(img.data(),img.size(),s.roi,present));
    Calibration refs; refs.present=present; refs.empty=empty;
    CHECK(!calibrated(refs,s));
    refs.has_present=refs.has_empty=true; CHECK(calibrated(refs,s));
    CHECK(classify(present,refs,s).label==Observation::Present);
    CHECK(classify(empty,refs,s).label==Observation::Empty);
    auto unknown=empty; unknown.roi.fill(140);
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty; unknown.quality=false;
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty; unknown.context.fill(255);
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty;
    for(int i=0;i<unknown.context_count;++i) unknown.context[i]+=21;
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty; unknown.context_count=0;
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    Calibration identical=refs; identical.present=identical.empty;
    CHECK(!calibrated(identical,s));
    img.fill(0); Sample black;
    CHECK(extract(img.data(),img.size(),s.roi,black)); CHECK(!black.quality);
    img.fill(255); CHECK(extract(img.data(),img.size(),s.roi,black)); CHECK(!black.quality);
    // Valid ROI at image boundary remains in bounds; random image stress.
    Settings edge=s; edge.roi={128,88,32,32}; CHECK(valid(edge));
    std::mt19937 rng(42);
    for(int trial=0;trial<100;++trial) {
        for(auto &p:img) p=uint8_t(rng());
        CHECK(extract(img.data(),img.size(),edge.roi,black));
        auto r=classify(black,refs,edge); CHECK(std::isfinite(r.empty));
    }
    Cycle c;
    CHECK(!c.arm(0,1000,Observation::Empty,s));
    CHECK(!c.arm(0,1000,Observation::Unknown,s));
    CHECK(!c.arm(2000,1000,Observation::Present,s));
    CHECK(c.arm(0,1000,Observation::Present,s));
    CHECK(!c.arm(0,1000,Observation::Present,s));
    CHECK(!c.needs_sample(999)); CHECK(c.needs_sample(1000));
    c.observe(999,Observation::Empty); CHECK(c.state==State::Armed);
    c.observe(1000,Observation::Empty); CHECK(c.state==State::Monitoring);
    CHECK(!c.needs_sample(1001));
    c.observe(6000,Observation::Unknown);
    c.observe(11000,Observation::Empty); CHECK(c.state==State::Monitoring);
    c.observe(16000,Observation::Empty); CHECK(c.state==State::Removed);
    c.acknowledge(); CHECK(!c.acknowledged); CHECK(c.state==State::Removed);
    c.tick(999999); CHECK(c.state==State::Removed);
    CHECK(c.arm(20000,21000,Observation::Present,s));
    c.observe(21000,Observation::Present); c.observe(81000,Observation::Present); c.tick(81000);
    CHECK(c.state==State::Alert); CHECK(!c.acknowledged);
    c.acknowledge(); CHECK(c.acknowledged); CHECK(c.state==State::Alert);
    CHECK(!c.arm(90000,91000,Observation::Empty,s)); CHECK(c.state==State::Alert);
    CHECK(c.arm(90000,91000,Observation::Present,s)); CHECK(!c.acknowledged);
    c.tick(151000); CHECK(c.state==State::Fault); // No valid observation.
    c.acknowledge(); CHECK(c.state==State::Fault && c.acknowledged);
    c.cancel(); CHECK(c.state==State::Idle);
    CHECK(c.arm(0,0,Observation::Present,s));
    c.observe(0,Observation::Empty); c.observe(20000,Observation::Empty);
    CHECK(c.state==State::Monitoring); // Gap breaks persistence.
    c.observe(25000,Observation::Present); c.observe(30000,Observation::Empty);
    CHECK(c.state==State::Monitoring); // Present also breaks persistence.
    c.observe(35000,Observation::Empty); CHECK(c.state==State::Removed);
    c.cancel(); CHECK(c.arm(0,0,Observation::Present,s));
    c.observe(0,Observation::Present); c.tick(60000); CHECK(c.state==State::Fault); // Stale.
    c.cancel(); CHECK(c.arm(0,0,Observation::Present,s));
    c.observe(55000,Observation::Empty); c.observe(60001,Observation::Empty);
    CHECK(c.state==State::Fault); // Capture completed after deadline.
    c.cancel(); CHECK(c.arm(0,0,Observation::Present,s));
    c.observe(55000,Observation::Empty); c.observe(60000,Observation::Empty); c.tick(60000);
    CHECK(c.state==State::Removed); // Exact deadline accepted.
    c.cancel(); CHECK(c.arm(0,0,Observation::Present,s));
    c.observe(55000,Observation::Present); c.observe(60000,Observation::Unknown); c.tick(60000);
    CHECK(c.state==State::Fault); // Occlusion cannot become pending/removal.
    Cycle reboot; CHECK(reboot.state==State::Idle && !reboot.acknowledged);
    // Monotonic timestamps across 32-bit millisecond wrap and midnight.
    int64_t late=INT64_C(4294967290);
    CHECK(reboot.arm(late,late+10000,Observation::Present,s));
    reboot.observe(late+10000,Observation::Empty); reboot.observe(late+15000,Observation::Empty);
    CHECK(reboot.state==State::Removed);
    std::printf("%d checks passed (synthetic software tests; not optical validation).\n",checks);
    return 0;
}
