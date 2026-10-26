#pragma once
#include <Arduino.h>
#include "driver/i2s.h"
#include "ClusterFluxDSP.h"

class ClusterFluxAudio {
public:
  ClusterFluxAudio(uint8_t bclk, uint8_t lrck, uint8_t dout, uint8_t din);
  void begin();
  bool ok() const { return _ok; }
  void setTime(uint8_t v)       { _time = clamp(v); }
  void setFeedback(uint8_t v)   { _feedback = clamp(v); }
  void setRate(uint8_t v);
  void setAmount(uint8_t v)     { _amount = clamp(v); }
  void setMix(uint8_t v)        { _mix = clamp(v); }
  void setDrive(uint8_t v);
  void setOutput(uint8_t v);
  void setTone(uint8_t v);
  void setSaturation(uint8_t v) { _saturation = clamp(v); }
  void setBbdModel(uint8_t v) { _bbdModel = clamp(v); }
  void setFeedbackColor(uint8_t v) { _feedbackColor = clamp(v); }
  void setBbdNoise(uint8_t v) { _bbdNoise = clamp(v); }
  void setStereoWidth(uint8_t v) { _stereoWidth = clamp(v); }
  void setStereoPhase(uint8_t v) { _stereoPhase = clamp(v); }
  void setLfo2Rate(uint8_t v);
  void setLfo2Depth(uint8_t v) { _lfo2Depth = clamp(v); }
  void setLfo2Wave(uint8_t v) { _lfo2Wave = v < 4 ? v : 0; }
  void setLfo2Target(uint8_t v) { _lfo2Target = v < 4 ? v : 0; }
  void setCrossFeedback(uint8_t v) { _crossFeedback = clamp(v); }
  void setCharacter(uint8_t v) { _character = v < 3 ? v : 0; }
  void setDualTap(uint8_t v) { _dualTap = clamp(v); }
  void setEnvelope(uint8_t v) { _envAmount = clamp(v); }
  void setFeedbackDamp(uint8_t v) { _feedbackDamp = clamp(v); }
  void setStereoMotion(uint8_t v) { _stereoMotion = clamp(v); }
  void setThroughZero(uint8_t v) { _throughZero = clamp(v); }
  void setFeedbackMatrix(uint8_t v) { _feedbackMatrix = v < 4 ? v : 0; }
  void setEnvAttack(uint8_t v) { _envAttack = clamp(v); }
  void setEnvRelease(uint8_t v) { _envRelease = clamp(v); }
  void setEnvTarget(uint8_t v) { _envTarget = v < 4 ? v : 0; }
  void setSyncMode(bool b) { _syncMode = b; if (!b) setEffectiveRateHz(_rateHz); }
  void setWaveform(uint8_t w) { _waveform = (w < FLUX_WAVE_COUNT) ? w : (uint8_t)FLUX_SINE; }
  void setWaveSmooth(uint8_t v); // 0..100 -> about 0.3..72 ms
  void setEffectiveRateHz(float hz);  // UI thread, from DIN MIDI or free-rate fallback
  void requestLfoPhaseReset() { ++_phaseResetSequence; } // applied at next audio block
  float effectiveRateHz() const { return _effectiveHz; }
  float freeRateHz() const { return _rateHz; }

  float feedbackGain() const; // signed linear 0..108% to/from center
  void setChorus(bool b)        { _chorus = b; }
  void setBypass(bool b)        { _bypass = b; }
  void setEnhanced(bool b)      { _enhanced = b; }
  void setInputSource(uint8_t s) { _source = s % 3; }
  float rateHz() const { return _effectiveHz; }
  float nominalDelayMs() const;
  float actualDelayMs() const { return _actualDelayMs; }
  uint32_t avgLoadUs() const { return _avgUs; }
  uint32_t peakLoadUs() const { return _peakUs; }
  uint32_t underruns() const { return _underruns; }
  uint32_t sampleRate() const { return ClusterFluxDSP::SAMPLE_RATE; }
  uint16_t blockFrames() const { return 128; }

private:
  static uint8_t clamp(uint8_t v) { return v > 100 ? 100 : v; }
  uint8_t _bclk, _lrck, _dout, _din;
  volatile bool _ok, _bypass, _chorus, _enhanced, _syncMode;
  volatile uint8_t _source; // 0=L, 1=R, 2=SUM
  volatile uint8_t _time, _feedback, _rate, _amount, _mix, _drive, _output, _tone, _saturation, _waveform, _bbdModel, _feedbackColor, _bbdNoise, _stereoWidth, _stereoPhase;
  volatile uint8_t _lfo2Depth, _lfo2Wave, _lfo2Target, _crossFeedback, _character;
  volatile uint8_t _dualTap, _envAmount, _feedbackDamp, _stereoMotion;
  volatile uint8_t _throughZero, _feedbackMatrix, _envAttack, _envRelease, _envTarget;
  volatile float _lfo2Increment;
  volatile uint32_t _phaseResetSequence;
  volatile float _rateHz, _effectiveHz, _rateIncrement, _driveGain, _outputGain, _toneCoeff, _waveSmoothCoeff;
  volatile uint32_t _underruns, _avgUs, _peakUs;
  volatile float _actualDelayMs = 0.0f;
  uint64_t _accumUs;
  uint32_t _blocks, _windowMs;
  TaskHandle_t _task;
  ClusterFluxDSP _dsp;

  bool beginI2S();
  void audioTask();
  static void audioTaskThunk(void *arg);
  static inline int32_t toI2S(float x) {
    if (x >= 1.0f) return 2147483647;
    if (x <= -1.0f) return (int32_t)(-2147483647 - 1);
    return (int32_t)(x * 2147483647.0f);
  }
};
