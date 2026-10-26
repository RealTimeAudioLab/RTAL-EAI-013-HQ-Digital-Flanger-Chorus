#include "ClusterFluxDSP.h"
#include <math.h>
#include <string.h>

#pragma GCC optimize ("O3")

ClusterFluxDSP::ClusterFluxDSP()
  : _write(0), _phase(0.0f), _waveSmooth(0.0f), _waveSmoothR(0.0f), _prng(0xB17D2026UL), _noisePrng(0x108F004UL),
    _randomFrom(0.0f), _randomTo(0.0f), _randomNext(0.0f), _wetL(0.0f), _wetR(0.0f),
    _lp1L(0.0f), _lp2L(0.0f), _lp1R(0.0f), _lp2R(0.0f),
    _preLP_L(0.0f), _preLP_R(0.0f), _reconLP_L(0.0f), _reconLP_R(0.0f),
    _preLP2_L(0.0f), _preLP2_R(0.0f), _reconLP2_L(0.0f), _reconLP2_R(0.0f),
    _hpPrevInL(0.0f), _hpPrevOutL(0.0f), _hpPrevInR(0.0f), _hpPrevOutR(0.0f),
    _fbTapL(0.0f), _fbTapR(0.0f),
    _centerSmooth(170.0f), _swingSmooth(0.0f),
    _feedbackSmooth(0.0f), _mixSmooth(0.5f),
    _driveSmooth(1.0f), _outSmooth(1.0f),
    _toneSmooth(0.65f), _satThresholdSmooth(0.67f),
    _modelSmooth(0.0f), _fbColorSmooth(0.0f), _noiseSmooth(0.0f),
    _stereoWidthSmooth(1.0f), _stereoPhaseSmooth(0.25f),
    _phase2(0.0f), _lfo2Smooth(0.0f), _crossSmooth(0.0f),
    _env(0.0f), _fbDampL(0.0f), _fbDampR(0.0f), _dualSmooth(0.0f),
    _envAmountSmooth(0.0f), _dampSmooth(0.0f), _motionSmooth(0.0f) {
  _target.chorus = false;
  _target.enhancedStereo = false;
  _target.timeNorm = 0.5f;
  _target.amountNorm = 0.25f;
  _target.lfoIncrement = 0.3f / (float)SAMPLE_RATE;
  _target.waveform = FLUX_SINE;
  _target.waveSmoothCoeff = 0.01f;
  _target.feedback = 0.0f;
  _target.mix = 0.5f;
  _target.driveGain = 1.0f;
  _target.outputGain = 1.0f;
  _target.toneCoeff = 0.65f;
  _target.satNorm = 0.30f;
  _target.bbdModel = 0.0f;  // zero is bit-compatible F003-style DSP
  _target.feedbackColor = 0.0f;
  _target.bbdNoise = 0.0f;
  _target.stereoWidth = 1.0f;
  _target.stereoPhase = 0.25f;
  _target.lfo2Increment = 0.0f;
  _target.lfo2Depth = 0.0f;
  _target.lfo2Wave = 0;
  _target.lfo2Target = 0;
  _target.crossFeedback = 0.0f;
  _target.character = 0;
  _target.dualTap = 0.0f;
  _target.envAmount = 0.0f;
  _target.feedbackDamp = 0.0f;
  _target.stereoMotion = 0.0f;
  _target.throughZero = 0.0f;
  _target.feedbackMatrix = 0;
  _target.envAttackCoeff = 0.004f;
  _target.envReleaseCoeff = 0.00035f;
  _target.envTarget = 0;
}

void ClusterFluxDSP::begin() {
  for (int i = 0; i <= SINE_SIZE; ++i)
    _sine[i] = sinf(6.2831853071795864769f * ((float)i / (float)SINE_SIZE));
  // Approximate bandwidth changes with BBD clock. Fictitious 128/1024
  // effective stages are a voicing choice, NOT a claim of actual Moog chips.
  // Model clock = stages / delay_seconds; anti-alias & reconstruction
  // corner = min(11 kHz, max(1.5 kHz, 0.18 * estimated clock)).
  for (int i=0; i<FILTER_LUT_SIZE; ++i) {
    const float delay = i < 1 ? 1.0f : (float)i;
    float fcF = 0.18f * 128.0f * (float)SAMPLE_RATE / delay;
    float fcC = 0.18f * 1024.0f * (float)SAMPLE_RATE / delay;
    if (fcF < 1500.0f) fcF=1500.0f;
    if (fcF > 11000.0f) fcF=11000.0f;
    if (fcC < 1500.0f) fcC=1500.0f;
    if (fcC > 11000.0f) fcC=11000.0f;
    _clockFlange[i] = 1.0f - expf(-6.28318530718f * fcF / (float)SAMPLE_RATE);
    _clockChorus[i] = 1.0f - expf(-6.28318530718f * fcC / (float)SAMPLE_RATE);
  }
  _randomFrom = nextRandom();
  _randomTo = nextRandom();
  _randomNext = nextRandom();
  reset();
}

void ClusterFluxDSP::reset() {
  memset(_lineL, 0, sizeof(_lineL));
  memset(_lineR, 0, sizeof(_lineR));
  memset(_dualLineL, 0, sizeof(_dualLineL));
  memset(_dualLineR, 0, sizeof(_dualLineR));
  _dualFbL = _dualFbR = 0.0f;
  memset(_dryLine, 0, sizeof(_dryLine));
  _tzSmooth = 0.0f;
  _write = 0;
  _wetL = _wetR = 0.0f;
  _lp1L = _lp2L = _lp1R = _lp2R = 0.0f;
  _preLP_L = _preLP_R = _reconLP_L = _reconLP_R = 0.0f;
  _preLP2_L = _preLP2_R = _reconLP2_L = _reconLP2_R = 0.0f;
  _hpPrevInL = _hpPrevOutL = _hpPrevInR = _hpPrevOutR = 0.0f;
  _fbTapL = _fbTapR = 0.0f;
  _env = _fbDampL = _fbDampR = 0.0f;
  // Keep _phase and modulation smoothing for click-minimized bypass exit.
}

void ClusterFluxDSP::resetPhase() {
  _phase = 0.0f;
  _phase2 = 0.0f;
  // Preserve _waveSmooth to slew across any jump; preserve delay and tails.
  _randomFrom = _randomTo;
  _randomTo = _randomNext;
  _randomNext = nextRandom();
}

void ClusterFluxDSP::setControls(const FluxControls &c) { _target = c; }

inline float ClusterFluxDSP::softLimit(float x) {
  const float a = fabsf(x);
  if (a <= 0.85f) return x;
  const float e = a - 0.85f;
  const float y = 0.85f + (0.15f * e) / (0.15f + e);
  return x < 0.0f ? -y : y;
}

inline float ClusterFluxDSP::bbdLimit(float x, float threshold) {
  const float a = fabsf(x);
  if (a <= threshold) return x;
  const float e = a - threshold;
  const float room = 1.0f - threshold;
  const float y = threshold + (room * e) / (room + e);
  return x < 0.0f ? -y : y;
}

// Mild nonlinear feedback; no trig, tanh or exp in the render thread.
inline float ClusterFluxDSP::nonlinearFeedback(float x) {
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f*x2);
}

float ClusterFluxDSP::clockCoefficient(float delaySamples) const {
  int i = (int)delaySamples;
  if (i < 1) i = 1;
  if (i > FILTER_LUT_SIZE - 2) i = FILTER_LUT_SIZE - 2;
  const float t = delaySamples - (float)i;
  const float *lut = _target.chorus ? _clockChorus : _clockFlange;
  return lut[i] + (lut[i+1] - lut[i]) * t;
}

float ClusterFluxDSP::nextNoise() {
  uint32_t x = _noisePrng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  _noisePrng = x;
  return (float)(x >> 8) * (2.0f / 16777215.0f) - 1.0f;
}

inline float ClusterFluxDSP::sineAt(float p) const {
  const float f = p * (float)SINE_SIZE;
  const int i = (int)f;
  return _sine[i] + (f - (float)i) * (_sine[i + 1] - _sine[i]);
}

float ClusterFluxDSP::nextRandom() {
  // xorshift32, one draw per LFO cycle (not per audio sample).
  uint32_t x = _prng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  _prng = x;
  return (float)(x >> 8) * (2.0f / 16777215.0f) - 1.0f;
}

inline float ClusterFluxDSP::waveAt(float p, bool nextCycle) const {
  switch (_target.waveform) {
    default:
    case FLUX_SINE:          return sineAt(p);
    case FLUX_TRIANGLE:      return 1.0f - 4.0f * fabsf(p - 0.5f);
    case FLUX_SQUARE:        return p < 0.5f ? 1.0f : -1.0f;
    case FLUX_SAW_UP:        return 2.0f * p - 1.0f;
    case FLUX_SAW_DOWN:      return 1.0f - 2.0f * p;
    case FLUX_RANDOM:        return nextCycle ? _randomTo : _randomFrom;
    case FLUX_SMOOTH_RANDOM: {
      const float t = p * p * (3.0f - 2.0f * p); // smoothstep
      const float from = nextCycle ? _randomTo : _randomFrom;
      const float to   = nextCycle ? _randomNext : _randomTo;
      return from + (to - from) * t;
    }
  }
}

float ClusterFluxDSP::readCubic(const float *line, float delaySamples) const {
  const int whole = (int)delaySamples;
  const float t = delaySamples - (float)whole;
  const int i = ((int)_write - whole) & (LINE_SIZE - 1);
  const float xm1 = line[(i + 1) & (LINE_SIZE - 1)];
  const float x0 = line[i];
  const float x1 = line[(i - 1) & (LINE_SIZE - 1)];
  const float x2 = line[(i - 2) & (LINE_SIZE - 1)];
  return x0 + 0.5f * t * (x1 - xm1 + t * (
    2.0f * xm1 - 5.0f * x0 + 4.0f * x1 - x2 +
    t * (3.0f * (x0 - x1) + x2 - xm1)));
}

void ClusterFluxDSP::process(float monoInput, float &outLeft, float &outRight) {
  // Audio thread: no heap, trig, exp/pow, locks, logging or SD access.
  const float low = (_target.chorus ? 5.0f : 0.6f) * 32.0f;
  const float high = (_target.chorus ? 50.0f : 10.0f) * 32.0f;
  const float span = high - low;
  float inc2 = _target.lfo2Increment;
  if (inc2 < 0.0f) inc2 = 0.0f;
  if (inc2 > 0.25f) inc2 = 0.25f;
  _phase2 += inc2;
  if (_phase2 >= 1.0f) _phase2 -= 1.0f;
  float raw2;
  switch (_target.lfo2Wave) {
    case 1: raw2 = 1.0f - 4.0f * fabsf(_phase2 - 0.5f); break;
    case 2: raw2 = _phase2 < 0.5f ? 1.0f : -1.0f; break;
    case 3: raw2 = 2.0f * _phase2 - 1.0f; break;
    default: raw2 = sineAt(_phase2); break;
  }
  _lfo2Smooth += 0.025f * (raw2 - _lfo2Smooth);
  const float lfo2 = _lfo2Smooth * _target.lfo2Depth;
  _crossSmooth += 0.0015f * (_target.crossFeedback - _crossSmooth);
  _dualSmooth += 0.0015f * (_target.dualTap - _dualSmooth);
  _envAmountSmooth += 0.0015f * (_target.envAmount - _envAmountSmooth);
  _dampSmooth += 0.0015f * (_target.feedbackDamp - _dampSmooth);
  _motionSmooth += 0.0015f * (_target.stereoMotion - _motionSmooth);
  // Fast attack, slow release; envelope is derived from input only.
  const float absInput = fabsf(monoInput);
  const float envCoeff = absInput > _env ? _target.envAttackCoeff : _target.envReleaseCoeff;
  _env += envCoeff * (absInput - _env);
  const float targetSwing = 0.5f * _target.amountNorm * span;
  const float targetCenter = low + targetSwing +
      _target.timeNorm * (span - 2.0f * targetSwing);
  _centerSmooth += 0.0031f * (targetCenter - _centerSmooth);
  _swingSmooth += 0.0031f * (targetSwing - _swingSmooth);
  _feedbackSmooth += 0.0015f * (_target.feedback - _feedbackSmooth);
  _mixSmooth += 0.008f * (_target.mix - _mixSmooth);
  _driveSmooth += 0.008f * (_target.driveGain - _driveSmooth);
  _outSmooth += 0.008f * (_target.outputGain - _outSmooth);
  _toneSmooth += 0.002f * (_target.toneCoeff - _toneSmooth);
  const float targetThreshold = 0.82f - 0.50f * _target.satNorm;
  _satThresholdSmooth += 0.002f * (targetThreshold - _satThresholdSmooth);
  _modelSmooth += 0.0015f * (_target.bbdModel - _modelSmooth);
  _fbColorSmooth += 0.0015f * (_target.feedbackColor - _fbColorSmooth);
  _noiseSmooth += 0.0015f * (_target.bbdNoise - _noiseSmooth);
  _stereoWidthSmooth += 0.003f * (_target.stereoWidth - _stereoWidthSmooth);
  _stereoPhaseSmooth += 0.003f * (_target.stereoPhase - _stereoPhaseSmooth);

  _phase += _target.lfoIncrement;
  if (_phase >= 1.0f) {
    _phase -= 1.0f;
    _randomFrom = _randomTo;
    _randomTo = _randomNext;
    _randomNext = nextRandom();
  }
  // One-pole smoothing prevents sudden delay jumps on SQUARE/SAW/RANDOM
  // and also damps reset/start steps. Higher smoothing attenuates fast LFOs.
  const float raw = waveAt(_phase);
  _waveSmooth += _target.waveSmoothCoeff * (raw - _waveSmooth);
  float pR = _phase + _stereoPhaseSmooth;
  if (pR >= 1.0f) pR -= 1.0f;
  // For ENHANCED right channel, look ahead to next random segment if +90deg
  // crosses the cycle boundary. This keeps SMOOTH RANDOM continuous.
  const float modL = _waveSmooth;
  const float rawR = waveAt(pR, pR < _phase);
  _waveSmoothR += _target.waveSmoothCoeff * (rawR - _waveSmoothR);
  const float modR = _waveSmoothR;

  float dL = _centerSmooth + _swingSmooth * modL;
  float dR = _centerSmooth + _swingSmooth * modR;
  if (_target.lfo2Target == 1) {
    const float extra = lfo2 * span * 0.20f;
    dL += extra; dR += extra;
  }
  if (_envAmountSmooth > 0.00001f && _target.envTarget == 0) {
    const float envShift = _envAmountSmooth * _env * span * 0.45f;
    dL += envShift; dR += envShift;
  }
  if (dL < 19.2f) dL = 19.2f;
  if (dR < 19.2f) dR = 19.2f;
  if (dL > 1600.0f) dL = 1600.0f;
  if (dR > 1600.0f) dR = 1600.0f;
  _lastDelaySamples = dL;

  const float driven = softLimit(monoInput * _driveSmooth);
  float fbGain = _feedbackSmooth;
  if (_target.lfo2Target == 2) {
    fbGain += 0.45f * lfo2;
    if (fbGain > 1.08f) fbGain = 1.08f;
    if (fbGain < -1.08f) fbGain = -1.08f;
  }
  if (_target.envTarget == 1) fbGain += 0.45f * _envAmountSmooth * _env;
  if (fbGain > 1.08f) fbGain = 1.08f;
  if (fbGain < -1.08f) fbGain = -1.08f;
  const float cross = _crossSmooth < 0.0f ? 0.0f : (_crossSmooth > 1.0f ? 1.0f : _crossSmooth);
  // Damped feedback: 0 means identical F009A feedback path.
  const float coeff = 0.28f - 0.26f * _dampSmooth;
  _fbDampL += coeff * (_fbTapL - _fbDampL);
  _fbDampR += coeff * (_fbTapR - _fbDampR);
  const float tapL = _fbTapL + _dampSmooth * (_fbDampL - _fbTapL);
  const float tapR = _fbTapR + _dampSmooth * (_fbDampR - _fbTapR);
  float matrixL = (1.0f-cross)*tapL + cross*tapR;
  float matrixR = (1.0f-cross)*tapR + cross*tapL;
  if (_target.feedbackMatrix == 1) {matrixL=tapR;matrixR=tapL;}
  else if (_target.feedbackMatrix == 2) {matrixL=tapR;matrixR=-tapL;}
  else if (_target.feedbackMatrix == 3) {matrixL=0.5f*(tapL-tapR);matrixR=-matrixL;}
  const float rawFbL = fbGain * matrixL;
  const float rawFbR = fbGain * matrixR;
  float fbL = rawFbL, fbR = rawFbR;
  if (_fbColorSmooth > 0.00001f) {
    fbL += _fbColorSmooth * (nonlinearFeedback(rawFbL) - rawFbL);
    fbR += _fbColorSmooth * (nonlinearFeedback(rawFbR) - rawFbR);
  }
  if (_target.character == 2) {
    fbL = nonlinearFeedback(1.7f * fbL);
    fbR = nonlinearFeedback(1.7f * fbR);
  }
  const float inL = driven + fbL, inR = driven + fbR;

  // The clock-dependent anti-alias filter is blended before the BBD write.
  // The clock-dependent reconstruction filter follows the delay read.
  // MODEL=0 reproduces F003's original delay/write/tone path. When disabled
  // this extra filtering is skipped; states track the unfiltered signal.
  const bool modelActive = _target.character != 1 && (_modelSmooth > 0.00001f || _target.bbdModel > 0.0f);
  float coeffL = 0.0f, coeffR = 0.0f;
  float delayInL = inL, delayInR = inR;
  if (modelActive) {
    coeffL = clockCoefficient(dL);
    coeffR = clockCoefficient(dR);
    _preLP_L += coeffL * (inL - _preLP_L);
    _preLP_R += coeffR * (inR - _preLP_R);
    // F006: second clock-tracking pole for steeper BBD input anti-aliasing.
    _preLP2_L += coeffL * (_preLP_L - _preLP2_L);
    _preLP2_R += coeffR * (_preLP_R - _preLP2_R);
    delayInL += _modelSmooth * (_preLP2_L - inL);
    delayInR += _modelSmooth * (_preLP2_R - inR);
  } else {
    _preLP_L = _preLP2_L = inL;
    _preLP_R = _preLP2_R = inR;
  }
  // Through-zero topology: wet is read around a fixed 1600-sample dry delay.
  // With TZ=0, both paths exactly reproduce F009C.
  _tzSmooth += 0.0008f * (_target.throughZero - _tzSmooth);
  _dryLine[_write] = driven;
  _lineL[_write] = bbdLimit(delayInL, _satThresholdSmooth);
  _lineR[_write] = bbdLimit(delayInR, _satThresholdSmooth);
  // TZ mode sweeps the wet tap through the dry reference delay (1600 samples).
  // Wet taps remain positive; dry reference delay enables real crossing.
  const float tzDelayL = 1600.0f + (dL - (low + high)*0.5f) * 0.7f;
  const float tzDelayR = 1600.0f + (dR - (low + high)*0.5f) * 0.7f;
  const float wetDelayL = dL + _tzSmooth * (tzDelayL - dL);
  const float wetDelayR = dR + _tzSmooth * (tzDelayR - dR);
  float delayedL = readCubic(_lineL, wetDelayL);
  float delayedR = readCubic(_lineR, wetDelayR);
  // F009E: independent second stereo delay pair, with its own feedback state.
  // LFO2 modulates the second pair's delay time even if its original target is OFF.
  // DUAL BBD=0 leaves the original wet path unchanged.
  if (_dualSmooth > 0.00001f || _target.dualTap > 0.0f) {
    const float secondMod = lfo2 * 0.28f * span;
    float tapDelayL = dL * 1.21f + 13.0f + secondMod;
    float tapDelayR = dR * 1.17f + 19.0f - secondMod;
    if (tapDelayL < 19.2f) tapDelayL = 19.2f;
    if (tapDelayR < 19.2f) tapDelayR = 19.2f;
    if (tapDelayL > 1800.0f) tapDelayL = 1800.0f;
    if (tapDelayR > 1800.0f) tapDelayR = 1800.0f;
    // Own recirculating delay line; limited gain keeps both paths stable.
    const float secondFeedback = fbGain * 0.65f;
    _dualLineL[_write] = bbdLimit(delayInL + secondFeedback * _dualFbL, _satThresholdSmooth);
    _dualLineR[_write] = bbdLimit(delayInR + secondFeedback * _dualFbR, _satThresholdSmooth);
    const float secondL = readCubic(_dualLineL, tapDelayL);
    const float secondR = readCubic(_dualLineR, tapDelayR);
    _dualFbL = softLimit(secondL);
    _dualFbR = softLimit(secondR);
    const float mix2 = 0.48f * _dualSmooth;
    delayedL += mix2 * (secondL - delayedL);
    delayedR += mix2 * (secondR - delayedR);
  }
  _write = (_write + 1) & (LINE_SIZE - 1);
  float wetDelayedL = delayedL, wetDelayedR = delayedR;
  if (modelActive) {
    _reconLP_L += coeffL * (delayedL - _reconLP_L);
    _reconLP_R += coeffR * (delayedR - _reconLP_R);
    // F006: matching second reconstruction pole, retaining MODEL=0 bypass.
    _reconLP2_L += coeffL * (_reconLP_L - _reconLP2_L);
    _reconLP2_R += coeffR * (_reconLP_R - _reconLP2_R);
    wetDelayedL += _modelSmooth * (_reconLP2_L - delayedL);
    wetDelayedR += _modelSmooth * (_reconLP2_R - delayedR);
  } else {
    _reconLP_L = _reconLP2_L = delayedL;
    _reconLP_R = _reconLP2_R = delayedR;
  }

  _lp1L += _toneSmooth * (wetDelayedL - _lp1L);
  _lp1R += _toneSmooth * (wetDelayedR - _lp1R);
  _lp2L += _toneSmooth * (_lp1L - _lp2L);
  _lp2R += _toneSmooth * (_lp1R - _lp2R);
  _wetL = _lp2L;
  _wetR = _lp2R;

  const float hpL = _wetL - _hpPrevInL + 0.9975f * _hpPrevOutL;
  const float hpR = _wetR - _hpPrevInR + 0.9975f * _hpPrevOutR;
  _hpPrevInL = _wetL;
  _hpPrevInR = _wetR;
  _hpPrevOutL = hpL;
  _hpPrevOutR = hpR;
  _fbTapL = hpL;
  _fbTapR = hpR;

  const float dryDelayed = readCubic(_dryLine, 1600.0f);
  const float dryInput = driven + _tzSmooth * (dryDelayed - driven);
  float effectiveMix = _mixSmooth;
  if (_target.envTarget == 3) effectiveMix += 0.4f * _envAmountSmooth * _env;
  if (effectiveMix > 1.0f) effectiveMix = 1.0f;
  const float dry = (1.0f - effectiveMix) * dryInput;
  const float wetGain = effectiveMix;
  // Optional hiss is wet-only and is NOT recirculated in the feedback loop.
  // At 100% max amplitude about -58 dBFS peak; default 0 = dead quiet.
  const float noiseL = (_noiseSmooth > 0.000001f)
      ? 0.0012f * _noiseSmooth * nextNoise() : 0.0f;
  const float wetLeft = _wetL + noiseL;
  float wetRight;
  if (_target.enhancedStereo) {
    float width = _stereoWidthSmooth;
    if (_target.lfo2Target == 3) {
      width += 0.5f * lfo2;
      if (width < 0.0f) width = 0.0f;
      if (width > 1.0f) width = 1.0f;
    }
    if (_target.envTarget == 2) width += 0.5f * _envAmountSmooth * _env;
    // Additional stereo motion is independently controllable from LFO2 TARGET.
    width += _motionSmooth * 0.45f * _lfo2Smooth;
    if (width < 0.0f) width = 0.0f;
    if (width > 1.0f) width = 1.0f;
    const float right = _wetL + width * (_wetR - _wetL);
    const float noiseR = (_noiseSmooth > 0.000001f)
        ? 0.0012f * _noiseSmooth * nextNoise() : 0.0f;
    wetRight = right + noiseR;
  } else {
    wetRight = -wetLeft; // Original stereo: phase-inverted wet signal on RIGHT.
  }
  outLeft = softLimit((dry + wetGain * wetLeft) * _outSmooth);
  outRight = softLimit((dry + wetGain * wetRight) * _outSmooth);
}
