#include "EntitySource.hpp"

#include <vector>

#include "Includes/Logger.h"

// ---------------------------------------------------------------------------
// Template data-source adapter — the ONE file you copy and edit per game.
//
// The projection and drawing layers are already done and game-independent. To
// bring up the ESP on a new title you only fill in the three TODOs below, then
// call esp::installSampleSource() (or your own equivalent) once the game
// library is loaded — typically at the end of hack_thread() in Main.cpp.
//
// This default implementation draws nothing: viewProjection() reports "not
// ready" and collect() returns no entities. That keeps the framework
// compiling and the overlay idle until a real adapter is wired up. Replace the
// bodies, don't replace the structure.
// ---------------------------------------------------------------------------

namespace esp {

namespace {

class SampleEntitySource : public IEntitySource {
public:
    // TODO(1): read the camera's view-projection matrix from the game.
    //
    // The robust, version-resilient route on Unity is to let the engine do the
    // maths for you: resolve Camera.main and call WorldToScreenPoint via
    // il2cpp, instead of hunting a matrix address that moves every patch. If
    // you do read a raw matrix, hand it back as-is and describe its layout in
    // projectionConfig() — don't pre-transpose here.
    bool viewProjection(Mat4 &out) override {
        // ===================================================================
        // ||  PLACEHOLDER / STUB — returns NO matrix on purpose.          ||
        // ||  `return false` tells the manager to skip the frame, so this ||
        // ||  template never draws. Replace with your game's real matrix. ||
        // ===================================================================
        out = identity();
        return false;  // not wired up yet => the manager skips the frame
    }

    // TODO(2): walk the game's entity/actor list and append the visible ones.
    //
    // For each target set world-space head and feet, and optionally health
    // (0..1), distance (metres), team and name. Leave unknown fields at their
    // defaults — the manager skips the bars/labels it has no data for.
    void collect(std::vector<Entity> &out) override {
        (void) out;
        // ===================================================================
        // ||  PLACEHOLDER / STUB — NOT EXECUTABLE GAME LOGIC.              ||
        // ||  This method INTENTIONALLY pushes NO entities. The lines      ||
        // ||  below are illustrative PSEUDO-CODE only: readVec3 /          ||
        // ||  readFloat / kPosOffset / kHpOffset etc DO NOT EXIST and the  ||
        // ||  offsets are made-up examples. Fill this in per game.         ||
        // ===================================================================
        //
        // EXAMPLE PSEUDO-CODE (does not compile as-is):
        // Entity e;
        // e.feet = readVec3(actor + kPosOffset);
        // e.head = e.feet; e.head.y += 1.8f;          // approximate height
        // e.health = readFloat(actor + kHpOffset) / 100.0f;
        // e.distance = distanceTo(e.feet);
        // e.team = readInt(actor + kTeamOffset);
        // e.name = readString(actor + kNameOffset);
        // out.push_back(e);
    }

    // TODO(3): match the matrix layout / handedness of whatever you returned in
    // viewProjection(). The defaults (column-major, Y-flipped, 0.01 near clip)
    // are correct for a typical Unity il2cpp camera matrix.
    ProjectionConfig projectionConfig() override {
        return ProjectionConfig();
    }
};

SampleEntitySource g_sampleSource;

}  // namespace

void installSampleSource() {
    setEntitySource(&g_sampleSource);
    LOGI(OBFUSCATE("ESP sample source installed (template — draws nothing until edited)"));
}

}  // namespace esp
