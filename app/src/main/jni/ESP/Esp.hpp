#ifndef ESP_ESP_HPP
#define ESP_ESP_HPP

#include <jni.h>
#include <atomic>
#include <cstdint>

// ---------------------------------------------------------------------------
// ESP manager: runtime configuration + JNI surface.
//
// The drawing happens in Java (ESPView, a transparent overlay). Every frame the
// View calls into here: the manager asks the active IEntitySource for the
// camera matrix and the entities, projects each entity to screen pixels, culls
// what can't be seen, and hands back a flat draw list the View paints with a
// Canvas. Keeping the geometry in native code means the View stays a dumb,
// fast renderer and the same logic drives every game.
// ---------------------------------------------------------------------------

namespace esp {

// Live configuration toggled from the mod menu. Every field is atomic: the menu
// thread writes, the overlay's render thread reads, and neither blocks the
// other. Defaults are chosen so that flipping only the master `enabled` on
// already produces a sensible box ESP.
struct Config {
    std::atomic<bool> enabled{false};
    std::atomic<bool> boxes{true};
    std::atomic<bool> cornerBox{false};
    std::atomic<bool> tracers{false};
    std::atomic<bool> healthBars{true};
    std::atomic<bool> names{true};
    std::atomic<bool> distances{false};
    std::atomic<int> maxDistance{0};    // 0 => no distance limit
    std::atomic<int> tracerOrigin{0};   // 0 bottom-centre, 1 top-centre, 2 centre
    std::atomic<int> lineThickness{2};  // stroke width in pixels for boxes/tracers
    std::atomic<uint32_t> colorBox{0xFFFFFFFFu};
    std::atomic<uint32_t> colorTracer{0xFF40FF40u};
    std::atomic<uint32_t> colorEnemy{0xFFFF4040u};
    std::atomic<uint32_t> colorAlly{0xFF40C0FFu};
    std::atomic<uint32_t> colorHealthHigh{0xFF40FF40u};
    std::atomic<uint32_t> colorHealthLow{0xFFFF4040u};
};

// Process-wide configuration singleton.
Config &config();

// Registers ESPView's native methods. Call from JNI_OnLoad alongside the other
// RegisterXxx helpers. Returns JNI_OK / JNI_ERR.
int RegisterESP(JNIEnv *env);

}  // namespace esp

#endif  // ESP_ESP_HPP
