#ifndef ESP_ESPMATH_HPP
#define ESP_ESPMATH_HPP

// ---------------------------------------------------------------------------
// Engine-agnostic projection math.
//
// This header has no Android / JNI / game dependencies on purpose: it is the
// one piece that is identical for every game, so it can be unit-built and
// reasoned about on its own. Everything game-specific lives behind
// IEntitySource (see EntitySource.hpp); everything Android-specific lives in
// the overlay View. This file only knows how to turn a world point into a
// screen pixel.
// ---------------------------------------------------------------------------

namespace esp {

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

// 4x4 matrix in row-major storage: element (row, col) is m[row * 4 + col].
// A world point is treated as a column vector on the right (clip = M * v), so
// the translation row sits at m[3], m[7], m[11], m[15] and the w-row is
// m[12..15]. Sources that hand us column-major matrices (Unity's
// Matrix4x4 is column-major in memory) should transpose first; the projection
// layer does this automatically based on ProjectionConfig::columnMajor.
struct Mat4 {
    float m[16];
};

inline Mat4 identity() {
    Mat4 r = {};
    r.m[0] = 1.0f;
    r.m[5] = 1.0f;
    r.m[10] = 1.0f;
    r.m[15] = 1.0f;
    return r;
}

inline Mat4 transpose(const Mat4 &a) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            r.m[row * 4 + col] = a.m[col * 4 + row];
        }
    }
    return r;
}

// Row-major matrix product (a * b). Kept here so a source that only has the
// view and projection matrices separately can combine them itself.
inline Mat4 multiply(const Mat4 &a, const Mat4 &b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[row * 4 + k] * b.m[k * 4 + col];
            }
            r.m[row * 4 + col] = sum;
        }
    }
    return r;
}

// Project a world point through a row-major view-projection matrix.
//
// On success `out` holds the screen-pixel position and the function returns
// true. It returns false when the point is at or behind the camera plane
// (clip.w below minClipW) — those points must not be drawn, otherwise they
// mirror onto the screen from behind the player.
//
// flipY maps normalised device coordinates (+Y up, origin centre) onto
// Android's Canvas (+Y down, origin top-left). This single boolean is the
// usual cause of an ESP that renders perfectly but upside-down.
inline bool worldToScreen(const Mat4 &vp, const Vec3 &world,
                          float screenW, float screenH,
                          bool flipY, float minClipW, Vec2 &out) {
    const float *m = vp.m;
    float clipX = m[0] * world.x + m[1] * world.y + m[2] * world.z + m[3];
    float clipY = m[4] * world.x + m[5] * world.y + m[6] * world.z + m[7];
    float clipW = m[12] * world.x + m[13] * world.y + m[14] * world.z + m[15];

    if (clipW < minClipW) {
        return false;
    }

    float invW = 1.0f / clipW;
    float ndcX = clipX * invW;
    float ndcY = clipY * invW;

    out.x = (ndcX * 0.5f + 0.5f) * screenW;
    float yUp = (ndcY * 0.5f + 0.5f) * screenH;
    out.y = flipY ? (screenH - yUp) : yUp;
    return true;
}

}  // namespace esp

#endif  // ESP_ESPMATH_HPP
