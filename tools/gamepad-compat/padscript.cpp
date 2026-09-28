// Test-only virtual pad: creates one libvirtualhid gamepad and plays a fixed script of inputs,
// each alone, printing "STEP <name> <unix ms>" as it goes. Used to see how Wine/SDL read it.
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <libvirtualhid/libvirtualhid.hpp>
using namespace std::chrono_literals;
static long long now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
int main(int argc, char **argv) {
  std::string_view which = argc > 1 ? argv[1] : "xseries";
  bool silent = argc > 2 && std::string_view(argv[2]) == "--silent";
  int lead_ms = argc > 3 ? std::stoi(argv[3]) : 20000;
  lvh::DeviceProfile profile;
  if (which == "x360") profile = lvh::profiles::xbox_360();
  else if (which == "xone") profile = lvh::profiles::xbox_one();
  else if (which == "ds5") profile = lvh::profiles::dualsense();
  else if (which == "ds4") profile = lvh::profiles::dualshock4();
  else profile = lvh::profiles::xbox_series();
  profile.name = "Sunshine " + profile.name;
  lvh::RuntimeOptions ro; ro.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(ro);
  lvh::CreateGamepadOptions o; o.profile = profile; o.metadata.stable_id = "nova-padtest-0";
  o.metadata.client_type = (which == "ds5" || which == "ds4") ? lvh::ClientControllerType::playstation : lvh::ClientControllerType::xbox;
  auto created = lvh::GamepadStateAdapter::create(*runtime, o);
  if (!created) { std::fprintf(stderr, "create failed: %s\n", std::string(created.status.message()).c_str()); return 1; }
  auto adapter = std::move(created.adapter);
  std::printf("CREATED %s %lld\n", profile.name.c_str(), now_ms()); std::fflush(stdout);
  using S = lvh::GamepadState; using B = lvh::GamepadButton;
  std::vector<std::pair<std::string, std::function<void(S &)>>> steps = {
    {"rest", [](S &) {}},
    {"lx+", [](S &s) { s.left_stick.x = 1.0F; }}, {"lx-", [](S &s) { s.left_stick.x = -1.0F; }},
    {"ly+up", [](S &s) { s.left_stick.y = 1.0F; }},
    {"rx+", [](S &s) { s.right_stick.x = 1.0F; }}, {"rx-", [](S &s) { s.right_stick.x = -1.0F; }},
    {"ry+up", [](S &s) { s.right_stick.y = 1.0F; }},
    {"lt", [](S &s) { s.left_trigger = 1.0F; }}, {"rt", [](S &s) { s.right_trigger = 1.0F; }},
    {"a", [](S &s) { s.buttons.set(B::a); }}, {"b", [](S &s) { s.buttons.set(B::b); }},
    {"x", [](S &s) { s.buttons.set(B::x); }}, {"y", [](S &s) { s.buttons.set(B::y); }},
    {"lb", [](S &s) { s.buttons.set(B::left_shoulder); }}, {"rb", [](S &s) { s.buttons.set(B::right_shoulder); }},
    {"back", [](S &s) { s.buttons.set(B::back); }}, {"start", [](S &s) { s.buttons.set(B::start); }},
    {"up", [](S &s) { s.buttons.set(B::dpad_up); }}, {"ls", [](S &s) { s.buttons.set(B::left_stick); }},
    {"rest2", [](S &) {}},
  };
  std::this_thread::sleep_for(std::chrono::milliseconds(lead_ms));
  for (auto &[name, apply] : steps) {
    std::printf("STEP %s %lld\n", name.c_str(), now_ms()); std::fflush(stdout);
    for (int i = 0; i < 12; ++i) {  // resend like a client would
      S s; apply(s);
      if (!silent) { auto st = adapter->set_state(s); if (!st.ok()) std::fprintf(stderr, "submit: %s\n", std::string(st.message()).c_str()); }
      std::this_thread::sleep_for(100ms);
    }
  }
  std::printf("DONE %lld\n", now_ms()); std::fflush(stdout);
  std::this_thread::sleep_for(3s);
  return 0;
}
