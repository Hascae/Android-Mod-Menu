#ifndef ESP_ENTITYSOURCE_HPP
#define ESP_ENTITYSOURCE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "EspMath.hpp"

// ---------------------------------------------------------------------------
// The data-source adapter layer.
//
// This is the only part of the ESP that a new game needs. The projection and
// drawing layers never read game memory; they ask an IEntitySource for two
// things each frame:
//
//   1. the current view-projection matrix (where the camera is looking), and
//   2. the list of entities to draw (where the targets are in the world).
//
// To support a new game you implement one IEntitySource and register it with
// setEntitySource(). Nothing above this interface changes. That is the whole
// point of the split: the hard, per-game reverse-engineering is boxed into a
// single class, and the generic 95% is reused untouched.
// ---------------------------------------------------------------------------

namespace esp {

// One drawable target for a single frame. All positions are in world space;
// the projection layer converts them to screen pixels.
struct Entity {
    Vec3 head;         // world-space top of the entity (used for the box top / name)
    Vec3 feet;         // world-space bottom / ground contact (box bottom, tracer end)
    float health;      // 0..1 fraction; negative => unknown, no health bar drawn
    float distance;    // metres from the camera; negative => unknown, no distance cull/text
    uint32_t color;    // ARGB override; 0 => the manager picks a colour from `team`
    int team;          // caller-defined; used only for default colouring
    std::string name;  // label text; empty => no name drawn

    Entity()
        : head{0.0f, 0.0f, 0.0f},
          feet{0.0f, 0.0f, 0.0f},
          health(-1.0f),
          distance(-1.0f),
          color(0u),
          team(0),
          name() {}
};

// Describes how a source's matrix is laid out and how NDC maps to the screen.
// Exposing these as knobs — instead of hard-coding one engine's convention —
// is what lets a single projection routine serve Unity, Unreal and custom
// engines. Aligning these three values is the difference between a locked-on
// ESP and one that is mirrored, flipped or transposed.
struct ProjectionConfig {
    bool columnMajor;  // true => matrix is column-major (e.g. Unity); transpose before use
    bool flipY;        // true => screen Y grows downward (Android Canvas)
    float minClipW;    // clip.w below this is treated as behind-camera and culled

    ProjectionConfig() : columnMajor(true), flipY(true), minClipW(0.01f) {}
};

class IEntitySource {
public:
    virtual ~IEntitySource() {}

    // Fill `out` with the current view-projection matrix. Return false to skip
    // the frame entirely (e.g. the camera pointer isn't resolved yet).
    virtual bool viewProjection(Mat4 &out) = 0;

    // Append this frame's entities to `out`. Called once per rendered frame.
    virtual void collect(std::vector<Entity> &out) = 0;

    // Optional: report the game's render resolution. Return false (the default)
    // to let the overlay use its own measured size, which is correct unless the
    // game renders at a different resolution than the window.
    virtual bool screenSize(float &width, float &height) {
        (void) width;
        (void) height;
        return false;
    }

    // Optional: how viewProjection()'s matrix is laid out. The default matches
    // a Unity il2cpp camera (column-major, Y-flipped for Canvas).
    virtual ProjectionConfig projectionConfig() {
        return ProjectionConfig();
    }
};

// Install / read the active source. Passing nullptr disables collection. The
// pointer is owned by the caller (typically a file-scope singleton that lives
// for the whole process), not by the framework.
void setEntitySource(IEntitySource *source);
IEntitySource *entitySource();

// Registers the bundled template source (see SampleSource.cpp). It draws
// nothing on its own; it exists as a compile-checked starting point to copy.
void installSampleSource();

// Registers the Unity (il2cpp) adapter (see UnitySource.cpp). It obtains the
// view-projection matrix straight from the engine, so projection works on any
// il2cpp Unity game with no offsets; only its collect() (the entity list) is
// left per-game.
void installUnitySource();

}  // namespace esp

#endif  // ESP_ENTITYSOURCE_HPP
