#include <iostream>
#include <stdexcept>
#include <functional>

static void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

static void reset() {
  for (auto& p : pads) p = {};
  for (auto& state : slotState) state = {};
  slotCount = 0; sampleResult = 0; arrivalResult = 0; sent.clear();
}

static EmscriptenGamepadEvent& attach(int index = 0, double timestamp = 0) {
  slotCount = std::max(slotCount, index + 1);
  auto& p = pads[index]; p.connected = EM_TRUE; p.index = index; p.timestamp = timestamp;
  p.numButtons = 17; p.numAxes = 4;
  std::strcpy(p.id, "Xbox Wireless Controller"); std::strcpy(p.mapping, "standard");
  return p;
}

static int failures = 0;
static int cases = 0;
static void check(const char* name, const std::function<void()>& run) {
  ++cases; reset();
  try { run(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& error) { ++failures; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
}

int main() {
  MoonlightInstance m; m.HandleGamepadInputState(false, false, false, false);
  check("positive timestamp announces before correctly mapped Xbox state", [&] {
    auto& p = attach(0, 123); p.digitalButton[0] = EM_TRUE; p.analogButton[6] = 0.5;
    p.axis[0] = 0.5; p.axis[1] = -0.5;
    m.PollGamepads();
    require(sent.size() == 2 && sent[0].arrival && !sent[1].arrival, "arrival must precede state");
    require(sent[1].mask == 1 && sent[1].buttons == A_FLAG, "incorrect index/mask/buttons");
    require(sent[1].leftTrigger == 127 && sent[1].x == 16383 && sent[1].y == 16383, "incorrect analog conversion");
  });
  for (double timestamp : {0.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1.0}) {
    check("unusable timestamp accepts button transitions", [&] {
      auto& p = attach(0, timestamp); m.PollGamepads();
      require(sent.empty(), "placeholder must wait for evidence");
      p.digitalButton[0] = EM_TRUE; p.analogButton[0] = 1; m.PollGamepads();
      require(sent.size() == 2 && sent[0].arrival && sent[1].buttons == A_FLAG, "real zero-timestamp Xbox was discarded");
      sent.clear(); p.digitalButton[0] = 0; p.analogButton[0] = 0; m.PollGamepads();
      require(sent.size() == 1 && !sent[0].arrival && sent[0].buttons == 0, "button release was lost");
    });
  }
  check("constant nonzero phantom input remains filtered", [&] {
    auto& p = attach(); p.axis[0] = -1; p.axis[1] = 0.5; p.digitalButton[0] = EM_TRUE;
    for (int i = 0; i < 20; ++i) m.PollGamepads();
    require(sent.empty(), "static placeholder was announced");
  });
  check("small noise is ignored but cumulative axis motion is accepted", [&] {
    auto& p = attach(); m.PollGamepads();
    for (double value : {0.002, 0.004, 0.006, 0.008}) { p.axis[0] = value; m.PollGamepads(); }
    require(sent.empty(), "tiny analog noise validated a placeholder");
    p.axis[0] = 0.02; m.PollGamepads();
    require(sent.size() == 2 && sent[1].x == 655, "cumulative motion was lost");
  });
  check("nonstandard mapping accepts analog trigger evidence", [&] {
    auto& p = attach(); p.mapping[0] = 0; m.PollGamepads();
    p.analogButton[6] = 0.5; m.PollGamepads();
    require(sent.size() == 2 && sent[1].leftTrigger == 127, "nonstandard analog input was discarded");
  });
  check("nonfinite axis values do not validate or poison pending evidence", [&] {
    auto& p = attach(); p.axis[0] = std::numeric_limits<double>::quiet_NaN(); m.PollGamepads();
    p.axis[0] = std::numeric_limits<double>::infinity(); m.PollGamepads();
    p.axis[0] = 0; m.PollGamepads(); require(sent.empty(), "nonfinite input validated a placeholder");
    p.axis[0] = 0.5; m.PollGamepads(); require(sent.size() == 2, "recovered finite axis was not recognized");
  });
  check("sparse controllers preserve slot 3 and bit 15", [&] {
    auto& p = attach(3); attach(15, 123); m.PollGamepads();
    require(sent.size() == 2 && sent[0].id == 15 && sent[1].mask == 32768, "physical high slot changed");
    sent.clear(); p.digitalButton[0] = EM_TRUE; m.PollGamepads();
    require(sent.size() == 3 && sent[0].arrival && sent[0].id == 3 && sent[0].mask == 32776, "multiple-controller mask changed");
  });
  check("controller connects after streaming has started", [&] {
    m.PollGamepads(); auto& p = attach(); m.PollGamepads();
    p.digitalButton[1] = EM_TRUE; m.PollGamepads();
    require(sent.size() == 2 && sent[1].buttons == B_FLAG, "hotplug input was lost");
  });
  check("disconnect sends removal and reconnect requires new evidence", [&] {
    auto& p = attach(0, 123); m.PollGamepads(); sent.clear();
    p.connected = 0; m.PollGamepads();
    require(sent.size() == 1 && !sent[0].arrival && sent[0].mask == 0, "host removal was not sent");
    sent.clear(); p.connected = 1; p.timestamp = 0; m.PollGamepads(); require(sent.empty(), "stale validity survived disconnect");
    p.digitalButton[0] = EM_TRUE; m.PollGamepads(); require(sent.size() == 2, "reconnect was not announced");
  });
  check("shrinking browser slot array removes announced controller", [&] {
    attach(3, 123); m.PollGamepads(); sent.clear(); slotCount = 0; m.PollGamepads();
    require(sent.size() == 1 && sent[0].id == 3 && sent[0].mask == 0, "shrunk slot was not removed");
  });
  check("identity replacement cannot inherit validity", [&] {
    auto& p = attach(0, 123); m.PollGamepads(); sent.clear();
    std::strcpy(p.id, "Replacement"); p.timestamp = 0; m.PollGamepads();
    require(sent.size() == 1 && sent[0].mask == 0, "replacement did not remove old slot");
    sent.clear(); p.digitalButton[0] = EM_TRUE; m.PollGamepads(); require(sent.size() == 2, "replacement was not reannounced");
  });
  check("layout changes establish a fresh pending baseline", [&] {
    auto& p = attach(); m.PollGamepads(); p.numAxes = 5; p.axis[0] = 0.5; m.PollGamepads();
    require(sent.empty(), "layout change was treated as input");
    p.axis[0] = 0.75; m.PollGamepads(); require(sent.size() == 2, "input after layout change was lost");
  });
  check("failed announcement retries before forwarding live state", [&] {
    auto& p = attach(); m.PollGamepads(); arrivalResult = -1; p.digitalButton[0] = EM_TRUE;
    m.PollGamepads(); require(sent.size() == 1 && sent[0].arrival, "live state sent before successful arrival");
    sent.clear(); arrivalResult = 0; m.PollGamepads();
    require(sent.size() == 2 && sent[0].arrival && !sent[1].arrival, "arrival was not retried");
  });
  check("new stream reannounces a validated zero-timestamp controller", [&] {
    auto& p = attach(); m.PollGamepads(); p.digitalButton[0] = EM_TRUE; m.PollGamepads();
    require(sent.size() == 2, "initial validation failed"); sent.clear();
    m.HandleGamepadInputState(false, false, false, false); m.PollGamepads();
    require(sent.size() == 2 && sent[0].arrival, "new stream skipped arrival");
  });
  check("existing positive-to-zero timestamp compatibility is preserved", [&] {
    auto& p = attach(0, 123); m.PollGamepads(); sent.clear(); p.timestamp = 0; m.PollGamepads();
    require(sent.size() == 1 && !sent[0].arrival, "timestamp reset disconnected the controller");
  });
  check("sampling error does not corrupt the previously validated slot", [&] {
    attach(0, 123); m.PollGamepads(); sent.clear(); sampleResult = -1; m.PollGamepads();
    require(sent.empty(), "input sent on a failed sample");
    sampleResult = 0; m.PollGamepads(); require(sent.size() == 1, "sampling recovery lost validity");
  });
  std::cout << cases - failures << '/' << cases << " native cases passed\n";
  return failures ? 1 : 0;
}
