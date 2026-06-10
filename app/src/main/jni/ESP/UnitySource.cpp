#include "EntitySource.hpp"

#include <cstring>
#include <vector>

#include "Includes/Logger.h"
#include "Includes/Utils.hpp"
#include "Includes/obfuscate.h"

// ---------------------------------------------------------------------------
// Unity (il2cpp) data-source adapter — the "borrow the engine" matrix path.
//
// The hardest part of any ESP is getting a correct view-projection matrix. The
// fragile way is to scan memory for the matrix and chase an address that moves
// every game update. This adapter does the opposite: it asks the Unity engine
// for its own matrices through the il2cpp runtime —
//   VP = Camera.main.projectionMatrix * Camera.main.worldToCameraMatrix
// — so the projection half works on essentially any il2cpp Unity title with no
// per-game offsets at all. The engine recomputes these every frame, so the ESP
// tracks the camera for free.
//
// What still belongs to each game is the ENTITY LIST: there is no universal way
// to enumerate "enemies", so collect() is the one method you fill in with your
// title's actor list (see the TODO at the bottom). That part is irreducibly
// game-specific; everything above it is reused untouched.
//
// Threading note: collect()/viewProjection() run on the overlay's UI thread,
// not Unity's main thread. Reading the camera matrix getters cross-thread is
// tolerated in practice (they are pure reads), and we attach the thread to the
// il2cpp runtime so managed calls are legal. For a hardened build, hook a
// per-frame Unity method and snapshot the matrix into an atomic instead.
// ---------------------------------------------------------------------------

namespace esp {

namespace {

#define UNITY_LIB OBFUSCATE("libil2cpp.so")

// Minimal il2cpp runtime ABI. We declare only what we call and resolve it by
// symbol name from libil2cpp.so, so no il2cpp headers are required.
typedef void *(*domain_get_t)();
typedef void *(*thread_attach_t)(void *domain);
typedef void *(*domain_assembly_open_t)(void *domain, const char *name);
typedef void *(*assembly_get_image_t)(void *assembly);
typedef void *(*class_from_name_t)(void *image, const char *ns, const char *name);
typedef void *(*class_get_method_from_name_t)(void *klass, const char *name, int argc);
typedef void *(*runtime_invoke_t)(void *method, void *obj, void **params, void **exc);
typedef void *(*object_unbox_t)(void *obj);

struct Il2Cpp {
    domain_get_t domain_get = nullptr;
    thread_attach_t thread_attach = nullptr;
    domain_assembly_open_t assembly_open = nullptr;
    assembly_get_image_t assembly_get_image = nullptr;
    class_from_name_t class_from_name = nullptr;
    class_get_method_from_name_t method_from_name = nullptr;
    runtime_invoke_t runtime_invoke = nullptr;
    object_unbox_t object_unbox = nullptr;

    bool runtimeReady = false;  // all symbols resolved
    bool cameraReady = false;   // Camera class + getters resolved
    bool attached = false;

    void *cameraClass = nullptr;
    void *getMain = nullptr;
    void *getWorldToCamera = nullptr;
    void *getProjection = nullptr;
};

Il2Cpp g_il2cpp;

template <typename Fn>
Fn resolve(const char *symbol) {
    return reinterpret_cast<Fn>(getAbsoluteAddress(UNITY_LIB, symbol));
}

bool resolveRuntime() {
    if (g_il2cpp.runtimeReady) {
        return true;
    }
    Il2Cpp &i = g_il2cpp;
    i.domain_get = resolve<domain_get_t>(OBFUSCATE("il2cpp_domain_get"));
    i.thread_attach = resolve<thread_attach_t>(OBFUSCATE("il2cpp_thread_attach"));
    i.assembly_open = resolve<domain_assembly_open_t>(OBFUSCATE("il2cpp_domain_assembly_open"));
    i.assembly_get_image = resolve<assembly_get_image_t>(OBFUSCATE("il2cpp_assembly_get_image"));
    i.class_from_name = resolve<class_from_name_t>(OBFUSCATE("il2cpp_class_from_name"));
    i.method_from_name =
            resolve<class_get_method_from_name_t>(OBFUSCATE("il2cpp_class_get_method_from_name"));
    i.runtime_invoke = resolve<runtime_invoke_t>(OBFUSCATE("il2cpp_runtime_invoke"));
    i.object_unbox = resolve<object_unbox_t>(OBFUSCATE("il2cpp_object_unbox"));

    i.runtimeReady = i.domain_get && i.thread_attach && i.assembly_open &&
                     i.assembly_get_image && i.class_from_name && i.method_from_name &&
                     i.runtime_invoke && i.object_unbox;
    if (!i.runtimeReady) {
        LOGE(OBFUSCATE("ESP Unity: il2cpp runtime symbols not resolved"));
    }
    return i.runtimeReady;
}

bool resolveCamera() {
    Il2Cpp &i = g_il2cpp;
    if (i.cameraReady) {
        return true;
    }
    if (!resolveRuntime()) {
        return false;
    }

    void *domain = i.domain_get();
    if (domain == nullptr) {
        return false;
    }
    if (!i.attached) {
        i.thread_attach(domain);
        i.attached = true;
    }

    // Camera lives in UnityEngine.CoreModule on modern Unity, plain UnityEngine
    // on older builds. Try both so the adapter spans engine versions.
    void *assembly = i.assembly_open(domain, OBFUSCATE("UnityEngine.CoreModule"));
    if (assembly == nullptr) {
        assembly = i.assembly_open(domain, OBFUSCATE("UnityEngine"));
    }
    if (assembly == nullptr) {
        return false;
    }
    void *image = i.assembly_get_image(assembly);
    if (image == nullptr) {
        return false;
    }
    i.cameraClass = i.class_from_name(image, OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"));
    if (i.cameraClass == nullptr) {
        return false;
    }
    i.getMain = i.method_from_name(i.cameraClass, OBFUSCATE("get_main"), 0);
    i.getWorldToCamera =
            i.method_from_name(i.cameraClass, OBFUSCATE("get_worldToCameraMatrix"), 0);
    i.getProjection = i.method_from_name(i.cameraClass, OBFUSCATE("get_projectionMatrix"), 0);

    i.cameraReady = i.getMain && i.getWorldToCamera && i.getProjection;
    return i.cameraReady;
}

// Invoke a Matrix4x4 getter and convert the result to a row-major Mat4.
// il2cpp returns the value type boxed; unbox gives 16 floats in Unity's
// column-major order, so a transpose lands us in our row-major convention.
bool readMatrix(void *method, void *instance, Mat4 &out) {
    void *exc = nullptr;
    void *boxed = g_il2cpp.runtime_invoke(method, instance, nullptr, &exc);
    if (boxed == nullptr || exc != nullptr) {
        return false;
    }
    float *raw = static_cast<float *>(g_il2cpp.object_unbox(boxed));
    if (raw == nullptr) {
        return false;
    }
    Mat4 columnMajor;
    std::memcpy(columnMajor.m, raw, sizeof(float) * 16);
    out = transpose(columnMajor);
    return true;
}

class UnityEntitySource : public IEntitySource {
public:
    bool viewProjection(Mat4 &out) override {
        if (!resolveCamera()) {
            return false;
        }

        void *exc = nullptr;
        void *mainCam = g_il2cpp.runtime_invoke(g_il2cpp.getMain, nullptr, nullptr, &exc);
        if (mainCam == nullptr || exc != nullptr) {
            return false;  // no active main camera this frame
        }

        Mat4 view;
        Mat4 proj;
        if (!readMatrix(g_il2cpp.getWorldToCamera, mainCam, view) ||
            !readMatrix(g_il2cpp.getProjection, mainCam, proj)) {
            return false;
        }

        out = multiply(proj, view);  // VP, row-major
        return true;
    }

    void collect(std::vector<Entity> &out) override {
        (void) out;
        // ===================================================================
        // ||  PLACEHOLDER / STUB — NOT EXECUTABLE GAME LOGIC.              ||
        // ||  This method INTENTIONALLY does nothing and pushes NO         ||
        // ||  entities. It is an empty hook you must fill in per game.     ||
        // ||  The code in the comment block below is illustrative PSEUDO-  ||
        // ||  CODE only: readVec3 / readFloat / kPosOffset / kHpOffset etc ||
        // ||  DO NOT EXIST and the offsets are made-up examples. Until you ||
        // ||  implement this, the ESP draws nothing (the matrix half above ||
        // ||  IS real; only this entity list is a stub).                   ||
        // ===================================================================
        //
        // EXAMPLE PSEUDO-CODE (does not compile as-is, fill with your game's data):
        //   for (each actor in the game's entity list) {
        //       Entity e;
        //       e.feet = readVec3(actor + kPosOffset);
        //       e.head = e.feet; e.head.y += 1.8f;
        //       e.health   = readFloat(actor + kHpOffset) / kMaxHp;
        //       e.distance = distanceTo(e.feet);
        //       e.team     = readInt(actor + kTeamOffset);
        //       e.name     = readManagedString(actor + kNameOffset);
        //       out.push_back(e);
        //   }
        //
        // The projection above already turns each world position into the right
        // screen pixel, so once this is filled the boxes appear immediately.
    }

    // Matrices are already row-major after readMatrix(), and Android's Canvas is
    // Y-down, so flip. minClipW culls anything at/behind the camera plane.
    ProjectionConfig projectionConfig() override {
        ProjectionConfig pc;
        pc.columnMajor = false;
        pc.flipY = true;
        pc.minClipW = 0.0001f;
        return pc;
    }
};

UnityEntitySource g_unitySource;

}  // namespace

void installUnitySource() {
    setEntitySource(&g_unitySource);
    LOGI(OBFUSCATE("ESP Unity source installed (engine-matrix projection; fill collect() per game)"));
}

}  // namespace esp
