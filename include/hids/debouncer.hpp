// debouncer.hpp -- suppresses one-off noisy windows. Fires a single alert when
// the same non-normal class is predicted for `required` consecutive windows at
// or above `min_confidence`, then re-arms only after returning to normal (or a
// different class). This gives one alert per confirmed episode rather than one
// per window.
#pragma once

namespace hids {

class Debouncer {
public:
    Debouncer(int normal_class, int required, float min_confidence)
        : normal_(normal_class),
          required_(required < 1 ? 1 : required),
          min_conf_(min_confidence) {}

    // Returns true exactly on the window where a new episode is confirmed.
    bool update(int cls, float confidence) {
        if (cls == normal_ || confidence < min_conf_) {
            cur_ = normal_;
            streak_ = 0;
            armed_ = true;
            return false;
        }
        if (cls == cur_) {
            ++streak_;
        } else {
            cur_ = cls;
            streak_ = 1;
            armed_ = true;
        }
        if (armed_ && streak_ >= required_) {
            armed_ = false;   // edge-trigger: don't re-fire until re-armed
            return true;
        }
        return false;
    }

private:
    int   normal_;
    int   required_;
    float min_conf_;
    int   cur_ = -1;
    int   streak_ = 0;
    bool  armed_ = true;
};

}  // namespace hids
