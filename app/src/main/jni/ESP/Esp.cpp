#include "Esp.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "EntitySource.hpp"
#include "EspMath.hpp"
#include "Includes/obfuscate.h"

namespace esp {

// --- configuration singleton ------------------------------------------------

Config &config() {
    static Config instance;
    return instance;
}

// --- active source registry -------------------------------------------------

namespace {
std::atomic<IEntitySource *> g_source{nullptr};
}

void setEntitySource(IEntitySource *source) {
    g_source.store(source, std::memory_order_release);
}

IEntitySource *entitySource() {
    return g_source.load(std::memory_order_acquire);
}

// --- draw-list encoding -----------------------------------------------------
//
// Geometry is handed to Java as a flat int[] in records of SHAPE_STRIDE ints:
//   [type, x1, y1, x2, y2, colorARGB]
// Pixel precision is plenty for an overlay, and integers avoid the float NaN
// canonicalisation traps you hit when smuggling colour bits through a float[].
// Text can't ride in an int[], so labels travel separately (see FrameCache).

namespace {

constexpr int SHAPE_RECT = 0;  // stroked rectangle (full box)
constexpr int SHAPE_LINE = 1;  // stroked line segment
constexpr int SHAPE_FILL = 2;  // filled rectangle (health bar)
// Each record is [type, x1, y1, x2, y2, colorARGB] — 6 ints. The matching
// SHAPE_STRIDE constant lives on the Java decoder side (ESPView).

// Labels computed during the last buildFrame, read back by the two label JNI
// calls on the same UI thread immediately afterwards. Guarded anyway so a
// source that collects from another thread can never tear a frame.
struct FrameCache {
    std::mutex mtx;
    std::vector<std::string> text;
    std::vector<int> meta;  // 3 ints per label: x, y, colorARGB
};

FrameCache &cache() {
    static FrameCache c;
    return c;
}

void pushShape(std::vector<jint> &out, int type, int x1, int y1, int x2, int y2,
               uint32_t color) {
    out.push_back(type);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(x2);
    out.push_back(y2);
    out.push_back(static_cast<jint>(color));
}

void pushBoxOutline(std::vector<jint> &out, int x1, int y1, int x2, int y2,
                    uint32_t color, bool corner) {
    if (!corner) {
        pushShape(out, SHAPE_RECT, x1, y1, x2, y2, color);
        return;
    }
    // Corner box: four L-shaped brackets, each leg a quarter of the shorter side.
    int w = x2 - x1;
    int h = y2 - y1;
    int len = std::max(1, std::min(w, h) / 4);

    pushShape(out, SHAPE_LINE, x1, y1, x1 + len, y1, color);  // top-left
    pushShape(out, SHAPE_LINE, x1, y1, x1, y1 + len, color);
    pushShape(out, SHAPE_LINE, x2 - len, y1, x2, y1, color);  // top-right
    pushShape(out, SHAPE_LINE, x2, y1, x2, y1 + len, color);
    pushShape(out, SHAPE_LINE, x1, y2 - len, x1, y2, color);  // bottom-left
    pushShape(out, SHAPE_LINE, x1, y2, x1 + len, y2, color);
    pushShape(out, SHAPE_LINE, x2 - len, y2, x2, y2, color);  // bottom-right
    pushShape(out, SHAPE_LINE, x2, y2 - len, x2, y2, color);
}

uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint32_t out = 0u;
    for (int shift = 0; shift < 32; shift += 8) {
        int ca = static_cast<int>((a >> shift) & 0xFFu);
        int cb = static_cast<int>((b >> shift) & 0xFFu);
        int cc = ca + static_cast<int>((cb - ca) * t);
        out |= (static_cast<uint32_t>(cc & 0xFF) << shift);
    }
    return out;
}

uint32_t entityColor(const Config &cfg, const Entity &e) {
    if (e.color != 0u) {
        return e.color;
    }
    return e.team == 0 ? cfg.colorEnemy.load() : cfg.colorAlly.load();
}

}  // namespace

// --- frame builder ----------------------------------------------------------
//
// Returns the geometry draw list and fills the label cache as a side effect.
// `viewW`/`viewH` are the overlay's measured size, used unless the source
// overrides them with the game's own render resolution.
static std::vector<jint> buildFrame(int viewW, int viewH) {
    std::vector<jint> shapes;

    Config &cfg = config();
    std::vector<std::string> labelText;
    std::vector<int> labelMeta;

    IEntitySource *source = entitySource();
    if (cfg.enabled.load() && source != nullptr && viewW > 0 && viewH > 0) {
        Mat4 vp;
        if (source->viewProjection(vp)) {
            ProjectionConfig pc = source->projectionConfig();
            if (pc.columnMajor) {
                vp = transpose(vp);
            }

            float screenW = static_cast<float>(viewW);
            float screenH = static_cast<float>(viewH);
            float srcW = 0.0f;
            float srcH = 0.0f;
            if (source->screenSize(srcW, srcH) && srcW > 0.0f && srcH > 0.0f) {
                screenW = srcW;
                screenH = srcH;
            }

            const bool drawBoxes = cfg.boxes.load();
            const bool cornerBox = cfg.cornerBox.load();
            const bool drawTracers = cfg.tracers.load();
            const bool drawHealth = cfg.healthBars.load();
            const bool drawNames = cfg.names.load();
            const bool drawDistances = cfg.distances.load();
            const int maxDist = cfg.maxDistance.load();
            const int tracerOrigin = cfg.tracerOrigin.load();
            const uint32_t boxColor = cfg.colorBox.load();
            const uint32_t tracerColor = cfg.colorTracer.load();
            const uint32_t healthHigh = cfg.colorHealthHigh.load();
            const uint32_t healthLow = cfg.colorHealthLow.load();

            std::vector<Entity> entities;
            source->collect(entities);

            for (const Entity &e : entities) {
                if (maxDist > 0 && e.distance >= 0.0f &&
                    e.distance > static_cast<float>(maxDist)) {
                    continue;
                }

                Vec2 feet;
                Vec2 head;
                if (!worldToScreen(vp, e.feet, screenW, screenH, pc.flipY,
                                   pc.minClipW, feet)) {
                    continue;
                }
                if (!worldToScreen(vp, e.head, screenW, screenH, pc.flipY,
                                   pc.minClipW, head)) {
                    continue;
                }

                float topY = std::min(head.y, feet.y);
                float botY = std::max(head.y, feet.y);
                float boxH = botY - topY;
                if (boxH < 2.0f) {
                    continue;  // too small / degenerate to be useful
                }
                float boxW = boxH * 0.45f;  // humanoid aspect ratio
                float cx = feet.x;
                int x1 = static_cast<int>(cx - boxW * 0.5f);
                int x2 = static_cast<int>(cx + boxW * 0.5f);
                int y1 = static_cast<int>(topY);
                int y2 = static_cast<int>(botY);

                // Cull anything fully outside the screen rectangle.
                if (x2 < 0 || x1 > viewW || y2 < 0 || y1 > viewH) {
                    continue;
                }

                uint32_t color = entityColor(cfg, e);

                if (drawBoxes) {
                    pushBoxOutline(shapes, x1, y1, x2, y2, boxColor != 0u ? boxColor : color,
                                   cornerBox);
                }

                if (drawTracers) {
                    int ox = viewW / 2;
                    int oy = viewH;  // 0 => bottom-centre
                    if (tracerOrigin == 1) {
                        oy = 0;  // top-centre
                    } else if (tracerOrigin == 2) {
                        oy = viewH / 2;  // centre
                    }
                    pushShape(shapes, SHAPE_LINE, ox, oy, static_cast<int>(cx), y2,
                              tracerColor);
                }

                if (drawHealth && e.health >= 0.0f) {
                    int barW = 3;
                    int barX2 = x1 - 2;
                    int barX1 = barX2 - barW;
                    // Background track.
                    pushShape(shapes, SHAPE_FILL, barX1, y1, barX2, y2, 0xC0202020u);
                    // Foreground fill grows from the bottom up with health.
                    int fillTop = y2 - static_cast<int>((y2 - y1) * e.health);
                    uint32_t hpColor = lerpColor(healthLow, healthHigh, e.health);
                    pushShape(shapes, SHAPE_FILL, barX1, fillTop, barX2, y2, hpColor);
                }

                if (drawNames && !e.name.empty()) {
                    labelText.push_back(e.name);
                    labelMeta.push_back(static_cast<int>(cx));
                    labelMeta.push_back(y1 - 4);
                    labelMeta.push_back(static_cast<jint>(color));
                }

                if (drawDistances && e.distance >= 0.0f) {
                    long rounded = static_cast<long>(e.distance + 0.5f);
                    labelText.push_back(std::to_string(rounded) + "m");
                    labelMeta.push_back(static_cast<int>(cx));
                    labelMeta.push_back(y2 + 18);
                    labelMeta.push_back(static_cast<jint>(color));
                }
            }
        }
    }

    FrameCache &fc = cache();
    {
        std::lock_guard<std::mutex> lock(fc.mtx);
        fc.text.swap(labelText);
        fc.meta.swap(labelMeta);
    }
    return shapes;
}

// --- JNI surface ------------------------------------------------------------

static jboolean ESP_nativeEnabled(JNIEnv *, jobject) {
    return config().enabled.load() ? JNI_TRUE : JNI_FALSE;
}

static jint ESP_nativeLineThickness(JNIEnv *, jobject) {
    return config().lineThickness.load();
}

static jintArray ESP_nativeBuildFrame(JNIEnv *env, jobject, jint width, jint height) {
    std::vector<jint> shapes = buildFrame(width, height);
    jintArray arr = env->NewIntArray(static_cast<jsize>(shapes.size()));
    if (arr != nullptr && !shapes.empty()) {
        env->SetIntArrayRegion(arr, 0, static_cast<jsize>(shapes.size()), shapes.data());
    }
    return arr;
}

static jobjectArray ESP_nativeLabelText(JNIEnv *env, jobject) {
    FrameCache &fc = cache();
    std::lock_guard<std::mutex> lock(fc.mtx);
    jclass stringClass = env->FindClass(OBFUSCATE("java/lang/String"));
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(fc.text.size()), stringClass,
                                           env->NewStringUTF(""));
    for (size_t i = 0; i < fc.text.size(); ++i) {
        jstring s = env->NewStringUTF(fc.text[i].c_str());
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

static jintArray ESP_nativeLabelMeta(JNIEnv *env, jobject) {
    FrameCache &fc = cache();
    std::lock_guard<std::mutex> lock(fc.mtx);
    jintArray arr = env->NewIntArray(static_cast<jsize>(fc.meta.size()));
    if (arr != nullptr && !fc.meta.empty()) {
        env->SetIntArrayRegion(arr, 0, static_cast<jsize>(fc.meta.size()), fc.meta.data());
    }
    return arr;
}

int RegisterESP(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("nativeEnabled"), OBFUSCATE("()Z"),
             reinterpret_cast<void *>(ESP_nativeEnabled)},
            {OBFUSCATE("nativeLineThickness"), OBFUSCATE("()I"),
             reinterpret_cast<void *>(ESP_nativeLineThickness)},
            {OBFUSCATE("nativeBuildFrame"), OBFUSCATE("(II)[I"),
             reinterpret_cast<void *>(ESP_nativeBuildFrame)},
            {OBFUSCATE("nativeLabelText"), OBFUSCATE("()[Ljava/lang/String;"),
             reinterpret_cast<void *>(ESP_nativeLabelText)},
            {OBFUSCATE("nativeLabelMeta"), OBFUSCATE("()[I"),
             reinterpret_cast<void *>(ESP_nativeLabelMeta)},
    };

    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/ESPView"));
    if (!clazz) {
        return JNI_ERR;
    }
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0) {
        return JNI_ERR;
    }
    return JNI_OK;
}

}  // namespace esp
