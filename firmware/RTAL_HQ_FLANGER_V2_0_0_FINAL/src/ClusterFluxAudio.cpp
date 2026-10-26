#include "ClusterFluxAudio.h"
#include <math.h>
#include <string.h>

#pragma GCC optimize ("O3")

#ifndef I2S_COMM_FORMAT_STAND_I2S
  #define PHX_FLUX_I2S_FORMAT ((i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB))
#else
  #define PHX_FLUX_I2S_FORMAT I2S_COMM_FORMAT_STAND_I2S
#endif

ClusterFluxAudio::ClusterFluxAudio(uint8_t bclk, uint8_t lrck, uint8_t dout, uint8_t din)
  : _bclk(bclk), _lrck(lrck), _dout(dout), _din(din),
    _ok(false), _bypass(false), _chorus(false), _enhanced(false), _syncMode(false), _source(0),
    _time(50), _feedback(50), _rate(35), _amount(25), _mix(50), _drive(20), _output(50), _tone(50), _saturation(30), _waveform(FLUX_SINE),
    _bbdModel(0), _feedbackColor(0), _bbdNoise(0), _stereoWidth(100), _stereoPhase(50),
    _lfo2Depth(0), _lfo2Wave(0), _lfo2Target(0), _crossFeedback(0), _character(0),
    _dualTap(0), _envAmount(0), _feedbackDamp(0), _stereoMotion(0),
    _throughZero(0), _feedbackMatrix(0), _envAttack(20), _envRelease(50), _envTarget(0),
    _lfo2Increment(0.5f/32000.0f), _phaseResetSequence(0),
    _rateHz(0.5f), _effectiveHz(0.5f), _rateIncrement(0.5f / 32000.0f),
    _driveGain(1.0f), _outputGain(1.0f), _toneCoeff(0.65f), _waveSmoothCoeff(0.01f),
    _underruns(0), _avgUs(0), _peakUs(0), _accumUs(0), _blocks(0), _windowMs(0), _task(nullptr) {}

void ClusterFluxAudio::setLfo2Rate(uint8_t v) {
  const float hz = 0.05f * powf(1000.0f, (float)clamp(v) * 0.01f);
  _lfo2Increment = hz / (float)ClusterFluxDSP::SAMPLE_RATE;
}

void ClusterFluxAudio::setRate(uint8_t v) {
  _rate = clamp(v);
  // 0.05 Hz .. 50 Hz front panel; expensive mapping only on UI thread.
  const float f = 0.05f * powf(1000.0f, (float)_rate * 0.01f);
  _rateHz = f;
  if (!_syncMode) setEffectiveRateHz(f);
}

void ClusterFluxAudio::setEffectiveRateHz(float hz) {
  if (hz < 0.001f) hz = 0.001f;
  if (hz > 50.0f) hz = 50.0f;
  _effectiveHz = hz;
  _rateIncrement = hz / (float)ClusterFluxDSP::SAMPLE_RATE;
}

void ClusterFluxAudio::setWaveSmooth(uint8_t v) {
  // Single-pole time constant: 0.30 .. 72 ms, logarithmic UI mapping.
  const float ms = 0.3f * powf(240.0f, (float)clamp(v) * 0.01f);
  _waveSmoothCoeff = 1.0f - expf(-1.0f / (0.001f * ms * (float)ClusterFluxDSP::SAMPLE_RATE));
}

void ClusterFluxAudio::setDrive(uint8_t v) {
  _drive = clamp(v);
  // Original front-panel nominal -7 .. +28 dB; 20% = 0 dB.
  const float dB = -7.0f + 0.35f * (float)_drive;
  _driveGain = powf(10.0f, dB * 0.05f);
}

void ClusterFluxAudio::setOutput(uint8_t v) {
  _output = clamp(v);
  // F001's safe operational approximation: -12 .. +12 dB, 50% = 0 dB.
  const float dB = -12.0f + 0.24f * (float)_output;
  _outputGain = powf(10.0f, dB * 0.05f);
}

void ClusterFluxAudio::setTone(uint8_t v) {
  _tone = clamp(v);
  // 1.8 kHz .. 8.8 kHz apparent analog-return corner; UI/control thread only.
  const float fc = 1800.0f + 70.0f * (float)_tone;
  _toneCoeff = 1.0f - expf(-6.28318530718f * fc /
                           (float)ClusterFluxDSP::SAMPLE_RATE);
}

float ClusterFluxAudio::feedbackGain() const {
  const int16_t delta = (int16_t)_feedback - 50;
  const float mag = (float)(delta < 0 ? -delta : delta) * 0.02f;
  // 0..90% knob travel: conventional +/-92% max; last 10% unlocks
  // +/-108% for sustaining/self-oscillating resonance.
  const float gain = (mag <= 0.90f) ? (mag * (0.92f / 0.90f))
                                     : (0.92f + (mag - 0.90f) * 1.6f);
  return delta < 0 ? -gain : gain;
}

float ClusterFluxAudio::nominalDelayMs() const {
  const float lo = _chorus ? 5.0f : 0.6f;
  const float hi = _chorus ? 50.0f : 10.0f;
  const float swing = 0.5f * (float)_amount * 0.01f * (hi - lo);
  return lo + swing + ((float)_time * 0.01f) * ((hi - lo) - 2.0f*swing);
}

void ClusterFluxAudio::begin() {
  setRate(_rate);
  setDrive(_drive);
  setOutput(_output);
  setTone(_tone);
  setWaveSmooth(55);
  _dsp.begin();
  _windowMs = millis();
  _ok = beginI2S();
  if (_ok) {
    if (xTaskCreatePinnedToCore(audioTaskThunk, "FluxAudio", 4096, this, 24, &_task, 1) != pdPASS) {
      Serial.println("F004 Audio task creation failed");
      _ok = false;
      i2s_driver_uninstall(I2S_NUM_0);
    }
  }
}

bool ClusterFluxAudio::beginI2S() {
  // Exact proven Phoenix Phaser P004 / C036 I2S setup (ESP32 Core 2.0.16).
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
  cfg.sample_rate = ClusterFluxDSP::SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = PHX_FLUX_I2S_FORMAT;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 128;
  cfg.use_apll = true;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("F004 i2s_driver_install: %d\n", (int)err);
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num = _bclk;
  pins.ws_io_num = _lrck;
  pins.data_out_num = _dout;
  pins.data_in_num = _din;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    Serial.printf("F004 i2s_set_pin: %d\n", (int)err);
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

void ClusterFluxAudio::audioTaskThunk(void *arg) { static_cast<ClusterFluxAudio*>(arg)->audioTask(); }

void ClusterFluxAudio::audioTask() {
  static const int WORDS = 128 * 2;
  int32_t rx[WORDS];
  int32_t tx[WORDS];
  bool previousBypass = false;
  uint32_t consumedResetSequence = _phaseResetSequence;
  // F009G B: envelope coefficients are constant until the UI changes a parameter.
  // Cache them on the audio task; never call expf twice on every 128-frame block.
  uint8_t cachedAttack = 255, cachedRelease = 255;
  float cachedAttackCoeff = 0.0f, cachedReleaseCoeff = 0.0f;
  for (;;) {
    size_t bytesRead = 0;
    const esp_err_t r = i2s_read(I2S_NUM_0, rx, sizeof(rx), &bytesRead, portMAX_DELAY);
    if (r != ESP_OK || bytesRead == 0) { ++_underruns; continue; }
    const uint32_t t0 = micros();
    const bool bypass = _bypass;
    if (bypass && !previousBypass) _dsp.reset();
    previousBypass = bypass;
    const uint32_t requestedReset = _phaseResetSequence;
    if (requestedReset != consumedResetSequence) {
      _dsp.resetPhase();
      consumedResetSequence = requestedReset;
    }
    if (bypass) {
      // Never touch the PCM words: identical bypass behavior to Phoenix Phaser.
      memcpy(tx, rx, bytesRead);
    } else {
      FluxControls c;
      c.chorus = _chorus;
      c.enhancedStereo = _enhanced;
      c.timeNorm = (float)_time * 0.01f;
      c.amountNorm = (float)_amount * 0.01f;
      c.lfoIncrement = _rateIncrement;
      c.waveform = _waveform;
      c.waveSmoothCoeff = _waveSmoothCoeff;
      c.feedback = feedbackGain();
      c.mix = (float)_mix * 0.01f;
      c.driveGain = _driveGain;
      c.outputGain = _outputGain;
      c.toneCoeff = _toneCoeff;
      c.satNorm = (float)_saturation * 0.01f;
      c.bbdModel = (float)_bbdModel * 0.01f;
      c.feedbackColor = (float)_feedbackColor * 0.01f;
      c.bbdNoise = (float)_bbdNoise * 0.01f;
      c.stereoWidth = (float)_stereoWidth * 0.01f;
      c.stereoPhase = (float)_stereoPhase * 0.005f; // 0..180 degrees
      c.lfo2Increment = _lfo2Increment;
      c.lfo2Depth = (float)_lfo2Depth * 0.01f;
      c.lfo2Wave = _lfo2Wave;
      c.lfo2Target = _lfo2Target;
      c.crossFeedback = (float)_crossFeedback * 0.01f;
      c.character = _character;
      c.dualTap = (float)_dualTap * 0.01f;
      c.envAmount = (float)_envAmount * 0.01f;
      c.feedbackDamp = (float)_feedbackDamp * 0.01f;
      c.stereoMotion = (float)_stereoMotion * 0.01f;
      c.throughZero = (float)_throughZero * 0.01f;
      c.feedbackMatrix = _feedbackMatrix;
      // 1-100 ms attack, 10-1000 ms release, exponential response coefficients.
      const uint8_t attackSetting = _envAttack;
      const uint8_t releaseSetting = _envRelease;
      if (attackSetting != cachedAttack) {
        const float attackMs = 1.0f + 0.99f * (float)attackSetting;
        cachedAttackCoeff = 1.0f - expf(-1.0f / (32.0f * attackMs));
        cachedAttack = attackSetting;
      }
      if (releaseSetting != cachedRelease) {
        const float releaseMs = 10.0f + 9.9f * (float)releaseSetting;
        cachedReleaseCoeff = 1.0f - expf(-1.0f / (32.0f * releaseMs));
        cachedRelease = releaseSetting;
      }
      c.envAttackCoeff = cachedAttackCoeff;
      c.envReleaseCoeff = cachedReleaseCoeff;
      c.envTarget = _envTarget;
      const uint8_t source = _source;
      _dsp.setControls(c);
      const int wordsRead = (int)(bytesRead / sizeof(int32_t));
      for (int i = 0; i + 1 < wordsRead; i += 2) {
        // I2S CHANNEL_FMT_RIGHT_LEFT: word 0 RIGHT, word 1 LEFT.
        const float inR = (float)rx[i] * (1.0f / 2147483648.0f);
        const float inL = (float)rx[i + 1] * (1.0f / 2147483648.0f);
        const float mono = (source == 0) ? inL : ((source == 1) ? inR : 0.5f * (inL + inR));
        float outL, outR;
        _dsp.process(mono, outL, outR);
        tx[i] = toI2S(outR);
        tx[i + 1] = toI2S(outL);
      }
      // Display telemetry needs only the last delay of this block.
      _actualDelayMs = _dsp.lastDelayMs();
    }

    const uint32_t elapsed = micros() - t0;
    _accumUs += elapsed;
    ++_blocks;
    if (elapsed > _peakUs) _peakUs = elapsed;
    if (millis() - _windowMs >= 1000U) {
      _avgUs = _blocks ? (uint32_t)(_accumUs / _blocks) : 0;
      _accumUs = 0;
      _blocks = 0;
      _windowMs = millis();
    }
    size_t bytesWritten = 0;
    const esp_err_t w = i2s_write(I2S_NUM_0, tx, bytesRead, &bytesWritten, portMAX_DELAY);
    if (w != ESP_OK || bytesWritten != bytesRead) ++_underruns;
  }
}
