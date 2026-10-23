#pragma once
#include <stdint.h>

// DIN-MIDI real-time F8 clock estimator (24 pulses/quarter), UI/Core 0 only.
// A 12-tick averaging window reduces serial/UI scheduling jitter, while
// short EMA estimates provide rate before the first complete window.
class FluxMidiClock {
public:
  FluxMidiClock()
    : _lastTickUs(0), _windowStartUs(0), _count(0),
      _goodIntervals(0), _windowTicks(0), _bpm(120.0f),
      _seen(false), _playing(false) {}

  void clockTick(uint32_t tUs) {
    ++_count;
    if (!_seen) {
      _seen = true;
      _lastTickUs = _windowStartUs = tUs;
      return;
    }
    const uint32_t dt = tUs - _lastTickUs; // works across micros() rollover
    if (dt < 5000U) return;                // reject duplicate/queued tick
    _lastTickUs = tUs;
    if (dt > 125000U) {                    // stopped/gap: new lock acquisition
      _goodIntervals = 0;
      _windowTicks = 0;
      _windowStartUs = tUs;
      return;
    }

    const float instantBpm = 2500000.0f / (float)dt;
    if (_goodIntervals == 0) _bpm = instantBpm;
    if (_goodIntervals < 10) {
      ++_goodIntervals;
      // Startup: fast estimate so sync can acquire after just 3 intervals.
      if (_goodIntervals > 1) _bpm += 0.07f * (instantBpm - _bpm);
    }

    if (++_windowTicks >= 12) {
      const uint32_t elapsed = tUs - _windowStartUs;
      if (elapsed != 0) {
        const float windowBpm = 2500000.0f * (float)_windowTicks / (float)elapsed;
        _bpm += 0.40f * (windowBpm - _bpm);
      }
      _windowStartUs = tUs;
      _windowTicks = 0;
    }
  }

  void start() { _playing = true; }
  void cont()  { _playing = true; }
  void stop()  { _playing = false; }

  bool active(uint32_t nowUs) const {
    return _seen && _goodIntervals >= 3 && (uint32_t)(nowUs - _lastTickUs) <= 350000U;
  }
  float bpm() const { return _bpm; }
  uint32_t ticks() const { return _count; }
  bool playing() const { return _playing; }

private:
  uint32_t _lastTickUs, _windowStartUs, _count;
  uint8_t _goodIntervals, _windowTicks;
  float _bpm;
  bool _seen, _playing;
};
