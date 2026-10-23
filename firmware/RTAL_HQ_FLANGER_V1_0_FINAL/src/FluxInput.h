#pragma once
#include <Arduino.h>

struct FluxInputState {
  bool f[9];
  bool hold[9];
  bool encoderPress;
  int16_t encoderStep;
  void clear() {
    for (uint8_t i = 0; i < 9; ++i) f[i] = hold[i] = false;
    encoderPress = false;
    encoderStep = 0;
  }
};

class FluxInput {
public:
  FluxInput(const uint8_t *buttonPins, uint8_t encA, uint8_t encB, uint8_t encSw);
  void begin();
  void update(FluxInputState &state);

private:
  struct ButtonState {
    uint8_t pin;
    bool lastRaw;
    bool stable;
    bool press;
    uint32_t lastChangeMs;
    uint32_t downMs;
    bool held;
  };

  const uint8_t *_buttonPins;
  uint8_t _encA, _encB, _encSw;
  ButtonState _buttons[8];
  ButtonState _encButton;
  volatile int32_t _encRawDelta;
  int32_t _encAccum;
  uint8_t _encLast;

  static FluxInput *_instance;
  static void IRAM_ATTR encoderISR();
  void IRAM_ATTR scanEncoderISR();
  void initButton(ButtonState &b, uint8_t pin);
  void scanButton(ButtonState &b);
};
