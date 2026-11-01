#pragma once
#include <cstdint>
#include <string>
#include <limits>
#include <cstring>
#include <vector>
#include <Limelight.h>

// Mock only the Samsung/browser and network boundaries. All slot processing,
// capability construction, mapping and state conversion come from gamepad.cpp.
#define EM_TRUE 1
#define EMSCRIPTEN_RESULT_SUCCESS 0
#define EMSCRIPTEN_RESULT_NOT_SUPPORTED -1
#define MAIN_THREAD_EM_ASM_INT(...) 0

struct EmscriptenGamepadEvent {
  double timestamp = 0;
  double axis[64]{};
  double analogButton[64]{};
  int digitalButton[64]{};
  int connected = 0;
  int index = 0;
  int numAxes = 4;
  int numButtons = 17;
  char id[64]{};
  char mapping[64]{};
};

struct Sent {
  bool arrival;
  int id;
  uint16_t mask;
  int buttons;
  unsigned char leftTrigger;
  unsigned char rightTrigger;
  short x, y, rx, ry;
};
static EmscriptenGamepadEvent pads[16];
static int slotCount = 0;
static int sampleResult = 0;
static int arrivalResult = 0;
static std::vector<Sent> sent;

static int emscripten_sample_gamepad_data() { return sampleResult; }
static int emscripten_get_num_gamepads() { return slotCount; }
static int emscripten_get_gamepad_status(int index, EmscriptenGamepadEvent* event) { *event = pads[index]; return 0; }

class MoonlightInstance {
 public:
  void HandleGamepadInputState(bool, bool, bool, bool);
  void PollGamepads();
  void ClControllerRumble(unsigned short, unsigned short, unsigned short);
  void ClLogMessage(const char*, ...) {}
  void stopStream() {}
  void toggleStats() {}
  void PostToJs(const std::string&) {}
};

extern "C" {
int LiSendControllerArrivalEvent(uint8_t id, uint16_t mask, uint8_t, uint32_t, uint16_t) {
  sent.push_back({true, id, mask, 0, 0, 0, 0, 0, 0, 0}); return arrivalResult;
}
int LiSendMultiControllerEvent(short id, short mask, int buttons, unsigned char lt, unsigned char rt, short x, short y, short rx, short ry) {
  sent.push_back({false, id, static_cast<uint16_t>(mask), buttons, lt, rt, x, y, rx, ry}); return 0;
}
int LiSendMouseMoveEvent(short, short) { return 0; }
int LiSendMouseButtonEvent(char, int) { return 0; }
int LiSendHScrollEvent(signed char) { return 0; }
int LiSendScrollEvent(signed char) { return 0; }
}
