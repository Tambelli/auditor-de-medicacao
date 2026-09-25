#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace auditor {
constexpr int W = 160, H = 120, SIDE = 32, N = SIDE * SIDE, GUARD = 192;
using Feature = std::array<uint8_t, N>;
using Context = std::array<uint8_t, GUARD>;
enum class Observation { Unknown, Present, Empty };
enum class State { Idle, Armed, Monitoring, Removed, Alert, Fault };
const char *name(Observation v);
const char *name(State v);
struct Roi { int x = 40, y = 24, w = 80, h = 72; };
struct Settings {
    Roi roi{};
    int hour = 8, minute = 0, window_s = 60, interval_s = 5, confirmations = 2;
    float max_distance = 18, margin = 5, context_distance = 20, stability = 8;
    int profile = 0; // 0 unconfirmed, 1 AI Thinker, 2 WROVER KIT
};
struct Sample { Feature roi{}; Context context{}; uint16_t context_count = 0; bool quality = false; };
struct Calibration {
    Sample present{}, empty{};
    bool has_present = false, has_empty = false;
};
struct Result { Observation label = Observation::Unknown; float present = 255, empty = 255, context = 255; };
bool valid(const Settings &s);
bool same_roi(const Roi &a, const Roi &b);
bool extract(const uint8_t *gray, size_t len, const Roi &roi, Sample &out);
float distance(const uint8_t *a, const uint8_t *b, size_t n);
float context_distance(const Sample &a, const Sample &b);
bool calibrated(const Calibration &c, const Settings &s);
Result classify(const Sample &sample, const Calibration &c, const Settings &s);

// Times are monotonic milliseconds, never wall-clock values.
class Cycle {
public:
    State state = State::Idle;
    bool acknowledged = false;
    bool arm(int64_t now, int64_t due, Observation initial, const Settings &s);
    void cancel();
    bool needs_sample(int64_t now) const;
    void observe(int64_t completed_at, Observation value);
    void tick(int64_t now);
    void acknowledge();
    int64_t deadline() const { return end_; }
    int64_t due() const { return due_; }
    bool active() const { return state == State::Armed || state == State::Monitoring; }
private:
    Settings settings_{};
    int64_t due_ = 0, end_ = 0, next_ = 0, last_ = -1;
    int streak_ = 0;
    Observation latest_ = Observation::Unknown;
};
} // namespace auditor
