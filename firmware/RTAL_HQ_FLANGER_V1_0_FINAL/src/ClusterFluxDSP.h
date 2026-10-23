#pragma once
#include <stdint.h>

// Platform-independent DSP. No Arduino dependencies or render-time allocation.
enum FluxWaveform : uint8_t {
  FLUX_SINE = 0, FLUX_TRIANGLE, FLUX_SQUARE, FLUX_SAW_UP,
  FLUX_SAW_DOWN, FLUX_RANDOM, FLUX_SMOOTH_RANDOM, FLUX_WAVE_COUNT
};

struct FluxControls {
  bool chorus;
  bool enhancedStereo;
  float timeNorm;      // 0..1
  float amountNorm;    // 0..1
  float lfoIncrement;  // cycles/sample, computed on UI core
  uint8_t waveform;    // FluxWaveform index
  float waveSmoothCoeff; // 1-pole coefficient computed on UI core
  float feedback;      // -1.08..+1.08
  float mix;           // 0..1
  float driveGain;     // linear
  float outputGain;    // linear
  float toneCoeff;     // BBD-model dual low-pass coefficient
  float satNorm;       // 0..1
  float bbdModel;      // 0..1: modeled, clock-dependent AA/reconstruction blend
  float feedbackColor; // 0..1: saturating feedback curve
  float bbdNoise;      // 0..1: optional VERY LOW level wet-only noise
  float stereoWidth;   // 0..1: ENH wet decorrelation (0=mono wet, 1=fully independent)
  float stereoPhase;   // 0..0.5 LFO cycles: ENH R relative to L (0..180 deg)
  float lfo2Increment; // cycles per sample
  float lfo2Depth;     // 0..1
  uint8_t lfo2Wave;    // 0 sine, 1 triangle, 2 square, 3 saw
  uint8_t lfo2Target;  // 0 off, 1 delay, 2 feedback, 3 width
  float crossFeedback; // 0..1
  uint8_t character;   // 0 classic, 1 modern, 2 experimental
  float dualTap;       // 0..1: additional offset wet delay tap
  float envAmount;     // 0..1: input envelope moves delay time
  float feedbackDamp;  // 0..1: additional low-pass in feedback path
  float stereoMotion;  // 0..1: LFO2 stereo-width animation
  float throughZero;   // 0..1: dry delay and wet delay alignment
  uint8_t feedbackMatrix; // 0 normal, 1 cross, 2 pingpong, 3 difference
  float envAttackCoeff, envReleaseCoeff;
  uint8_t envTarget; // 0 time, 1 feedback, 2 width, 3 mix
};

class ClusterFluxDSP {
public:
  enum { SAMPLE_RATE = 32000, LINE_SIZE = 2048, SINE_SIZE = 1024,
         FILTER_LUT_SIZE = 1602 };
  ClusterFluxDSP();
  void begin(); // setup thread only, generate sine LUT
  void reset(); // audio thread only, clear feedback/filter/delay state
  void resetPhase(); // audio-thread phase reset; preserve feedback tails
  void setControls(const FluxControls &controls); // per audio block
  void process(float monoInput, float &outLeft, float &outRight);

  // Diagnostic for native waveform tests, not accessed across cores on device.
  float lastModulation() const { return _waveSmooth; }
  float phase() const { return _phase; }
  float lastDelayMs() const { return _lastDelaySamples / (float)SAMPLE_RATE * 1000.0f; }

private:
  float _lineL[LINE_SIZE];
  float _lineR[LINE_SIZE];
  float _dualLineL[LINE_SIZE];
  float _dualLineR[LINE_SIZE];
  float _dualFbL, _dualFbR;
  float _dryLine[LINE_SIZE];
  float _sine[SINE_SIZE + 1];
  // One-pole coefficients precomputed once at setup. No expf in audio callback.
  float _clockFlange[FILTER_LUT_SIZE];
  float _clockChorus[FILTER_LUT_SIZE];
  uint16_t _write;
  float _phase;
  float _waveSmooth, _waveSmoothR;
  uint32_t _prng, _noisePrng;
  float _randomFrom, _randomTo, _randomNext;
  float _wetL, _wetR;
  float _lp1L, _lp2L, _lp1R, _lp2R;
  float _preLP_L, _preLP_R, _reconLP_L, _reconLP_R;
  float _preLP2_L, _preLP2_R, _reconLP2_L, _reconLP2_R;
  float _hpPrevInL, _hpPrevOutL, _hpPrevInR, _hpPrevOutR;
  float _fbTapL, _fbTapR;
  float _centerSmooth, _swingSmooth;
  float _lastDelaySamples = 0.0f;
  float _feedbackSmooth, _mixSmooth, _driveSmooth, _outSmooth;
  float _toneSmooth, _satThresholdSmooth;
  float _modelSmooth, _fbColorSmooth, _noiseSmooth;
  float _stereoWidthSmooth, _stereoPhaseSmooth;
  float _phase2, _lfo2Smooth, _crossSmooth;
  float _tzSmooth;
  float _env, _fbDampL, _fbDampR, _dualSmooth, _envAmountSmooth, _dampSmooth, _motionSmooth;
  FluxControls _target;

  float readCubic(const float *line, float delaySamples) const;
  inline float sineAt(float phase) const;
  inline float waveAt(float phase, bool nextCycle = false) const;
  float nextRandom();
  float nextNoise();
  float clockCoefficient(float delaySamples) const;
  static inline float nonlinearFeedback(float x);
  static inline float softLimit(float x);
  static inline float bbdLimit(float x, float threshold);
};
