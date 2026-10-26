#include "FluxInput.h"

FluxInput *FluxInput::_instance = nullptr;

FluxInput::FluxInput(const uint8_t *buttonPins, uint8_t encA, uint8_t encB, uint8_t encSw)
: _buttonPins(buttonPins), _encA(encA), _encB(encB), _encSw(encSw),
  _encRawDelta(0), _encAccum(0), _encLast(0) {}

void FluxInput::initButton(ButtonState &b, uint8_t pin) {
  b.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  b.lastRaw = digitalRead(pin);
  b.stable = b.lastRaw;
  b.press = false;
  b.downMs = 0; b.held = false;
  b.lastChangeMs = millis();
}

void FluxInput::begin() {
  for (uint8_t i = 0; i < 8; ++i) initButton(_buttons[i], _buttonPins[i]);
  initButton(_encButton, _encSw);
  pinMode(_encA, INPUT_PULLUP);
  pinMode(_encB, INPUT_PULLUP);
  _encLast = (digitalRead(_encA) ? 1 : 0) | (digitalRead(_encB) ? 2 : 0);
  _instance = this;
  attachInterrupt(digitalPinToInterrupt(_encA), FluxInput::encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(_encB), FluxInput::encoderISR, CHANGE);
}

void IRAM_ATTR FluxInput::encoderISR() {
  if (_instance) _instance->scanEncoderISR();
}

void IRAM_ATTR FluxInput::scanEncoderISR() {
  const uint8_t s = (digitalRead(_encA) ? 1 : 0) | (digitalRead(_encB) ? 2 : 0);
  if (s != _encLast) {
    static const int8_t tbl[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    _encRawDelta += tbl[(_encLast << 2) | s];
    _encLast = s;
  }
}

void FluxInput::scanButton(ButtonState &b) {
  b.press = false;
  const bool raw = digitalRead(b.pin);
  const uint32_t now = millis();
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChangeMs = now;
  }
  if ((now - b.lastChangeMs) > 18 && raw != b.stable) {
    b.stable = raw;
    if (b.stable == LOW) {b.downMs=now;b.held=false;b.press=true;}
  }
}

void FluxInput::update(FluxInputState &state) {
  state.clear();

  // Preset buttons: classify the gesture on debounced release.
  // This avoids losing long presses when a single loop iteration misses
  // the exact 900 ms threshold (e.g. during SD or MIDI processing).
  // Hold event is emitted only once, with no subsequent short-load event.
  for (uint8_t i = 0; i < 8; ++i) {
    ButtonState &b = _buttons[i];
    const bool wasDown = (b.stable == LOW);
    const uint32_t pressedAt = b.downMs;
    scanButton(b);
    const uint32_t now = millis();
    if (b.stable == LOW && !b.held && pressedAt != 0 &&
        (uint32_t)(now - pressedAt) >= 900U) {
      b.held = true;
      state.hold[i+1] = true;
    }
    if (wasDown && b.stable == HIGH && pressedAt != 0) {
      const uint32_t duration = (uint32_t)(now - pressedAt);
      if (!b.held) {
        if (duration >= 900U) state.hold[i+1] = true;
        else state.f[i+1] = true;
      }
      b.downMs = 0;
      b.held = false;
    }
  }
  scanButton(_encButton);
  state.encoderPress = _encButton.press;

  int32_t raw;
  noInterrupts();
  raw = _encRawDelta;
  _encRawDelta = 0;
  interrupts();
  _encAccum += raw;

  if (_encAccum >= 4 || _encAccum <= -4) {
    int16_t detents = (int16_t)(_encAccum / 4);
    _encAccum -= (int32_t)detents * 4;
    if (detents > 10) detents = 10;
    if (detents < -10) detents = -10;
    state.encoderStep = detents;
  }
}
