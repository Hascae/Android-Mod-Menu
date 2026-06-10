# Architecture

A map of how the pieces fit together. Read this before touching code you haven't seen before.

---

## Project layout

```
app/src/main/
├── java/com/android/support/
│   ├── Main.java           # entry point — loads the .so, checks overlay permission
│   ├── Launcher.java       # Service that owns and manages the floating View
│   ├── Menu.java           # the floating View itself (widgets, touch handling)
│   ├── Preferences.java    # SharedPreferences wrapper + dispatches Changes() to native
│   ├── CrashHandler.java   # Java-side crash logger (uncaught exceptions → files/)
│   └── DialogHelper.java   # overlay-safe AlertDialog (sets TYPE_APPLICATION_OVERLAY)
└── jni/
    ├── Main.cpp            # your mod code lives here
    ├── Android.mk          # ndk-build config (kept for AIDE compatibility, not CMake)
    ├── Application.mk      # ABI targets
    ├── Includes/
    │   ├── Utils.cpp/.hpp  # library loading, address resolution via xDL
    │   ├── Macros.h        # HOOK / PATCH / INST macros + Dobby wrappers
    │   ├── Logger.h        # LOGI / LOGE / LOGW
    │   └── obfuscate.h     # compile-time string obfuscation (AY obfuscator)
    ├── Menu/
    │   ├── Setup.cpp       # JNI_OnLoad, RegisterNatives for all three Java classes
    │   ├── Jni.cpp         # Toast, Dialog, startService — JNI helper functions
    │   ├── Menu.cpp        # Init, Icon, GetFeatureList, SettingsList
    │   └── Menu.hpp
    ├── Dobby/              # prebuilt libdobby.a (arm64-v8a + armeabi-v7a)
    ├── KittyMemory/        # KittyMemory + Keystone source + prebuilt libkeystone.a
    └── xDL/                # xDL source (dynamic linker introspection)
```

---

## Build system

ndk-build (`Android.mk` / `Application.mk`), not CMake. AIDE on Android only understands ndk-build, and that's reason enough to keep it. Don't migrate.

The shared library module is `MyLibName`. If you rename it, update `System.loadLibrary("MyLibName")` in `Main.java` as well — they must match.

---

## Startup sequence

```
App launch
  └─ MainActivity.onCreate()
       ├─ CrashHandler.init()           # register Java exception handler first
       ├─ System.loadLibrary(...)
       │    ├─ constructor(101) cl_install()   # native signal handler, before everything else
       │    ├─ JNI_OnLoad()                    # RegisterNatives for Menu, Preferences, Main
       │    └─ constructor() lib_main()        # spawns hack_thread (detached std::thread)
       └─ Main.CheckOverlayPermission()
            ├─ [no permission] → open Settings, exit process in 5 s
            └─ [has permission] → startService(Launcher)
                 └─ Launcher.onCreate()
                      ├─ menu.SetWindowManagerWindowService()   # adds View to WindowManager
                      └─ menu.ShowMenu()
```

### JNI_OnLoad

Runs the moment `System.loadLibrary` completes. Calls `RegisterNatives` for three Java classes — this is how C++ functions are reachable from Java without exported symbol names in the `.so`.

| Java class | Methods registered |
|---|---|
| `com.android.support.Menu` | `Icon`, `IconWebViewData`, `IsGameLibLoaded`, `Init`, `SettingsList`, `GetFeatureList` |
| `com.android.support.Preferences` | `Changes` |
| `com.android.support.Main` | `CheckOverlayPermission` |

All class/method name strings are wrapped in `OBFUSCATE()` so they don't appear as plain text in the binary.

### hack_thread

Spawned by `lib_main()`. Loops on `isLibraryLoaded()` — which scans `/proc/self/maps` — until the target `.so` shows up, then sets up hooks and patches. Unity games always need this loop because `libil2cpp.so` isn't in memory at process start.

```cpp
void hack_thread() {
    while (!isLibraryLoaded(targetLibName)) sleep(1);
    // hooks and patches go here
}
```

---

## Java side

### Launcher (Service)

Owns the floating overlay. Runs a handler that fires every second: if the game lib is loaded *and* the app is not in the foreground, the menu is hidden. Otherwise it stays visible. The `IsGameLibLoaded()` check matters — without it, the overlay hides itself a second after appearing in standalone mode (no game lib ever loads).

### Menu (View)

On `ShowMenu()`, calls `GetFeatureList()` via JNI, then parses each string into a widget and adds it to the scroll layout. When the user interacts with a widget, `Preferences.Changes()` is called, which persists the state in `SharedPreferences` and then calls native `Changes()`.

The menu position is saved to `SharedPreferences` on drag-end and restored on next show.

### Preferences

A thin bridge. Saves each feature's state keyed by feature number, restores it when the menu is rebuilt, and calls native `Changes()` on every user action. Settings items use negative feature numbers to keep them separated from mod features.

---

## Native side

### Main.cpp

This is where you write mods. Three functions matter:

- **`GetFeatureList()`** — returns a `jobjectArray` of strings, one per menu widget. Parsed by `Menu.java`.
- **`Changes()`** — called every time the user changes a feature. `featNum` identifies the item; `boolean`, `value`, `Lvalue`, `text` carry the new state.
- **`hack_thread()`** — runs after the target lib loads. Hook and patch calls go here.

### Menu.cpp

Handles `Init()` (sets title/subtitle text, shows a toast on load), `Icon()` (base64-encoded PNG for the menu button), `IconWebViewData()` (URL or null for WebView icon), and `SettingsList()` (items for the settings panel).

---

## Feature protocol

Each entry in `GetFeatureList` or `SettingsList` is either an underscore-delimited string (classic format) or a JSON object (new format). Both can be mixed in the same array.

### Underscore format

```
[id_]Type[_True][_name][_extras...]
```

`id` is optional. Without it, items are numbered automatically from 0. `Category`, `ButtonLink`, `RichTextView`, and `RichWebView` are skipped in the count — they cannot have an ID.

Adding `True_` after the type sets the default state to on (Toggle, CheckBox, ButtonOnOff only).

| Type | Extra fields | Counted |
|---|---|---|
| `Toggle` | — | yes |
| `CheckBox` | — | yes |
| `ButtonOnOff` | — | yes |
| `Button` | — | yes |
| `SeekBar` | `min_max` | yes |
| `Spinner` | `item1,item2,...` | yes |
| `RadioButton` | `item1,item2,...` | yes |
| `InputValue` | optional `maxvalue` | yes |
| `InputLValue` | optional `maxvalue` | yes |
| `InputText` | — | yes |
| `Category` | text | no |
| `RichTextView` | limited HTML | no |
| `RichWebView` | full HTML | no |
| `ButtonLink` | `url` | no |
| `Collapse` | label | no |
| `CollapseAdd_Type` | same as base type | follows base type |

```cpp
OBFUSCATE("Toggle_No death"),
OBFUSCATE("100_Toggle_True_The toggle 2"),      // id=100, on by default
OBFUSCATE("SeekBar_Score multiplier_1_100"),
OBFUSCATE("Spinner_Mode_Easy,Normal,Hard"),
OBFUSCATE("Collapse_Advanced"),
OBFUSCATE("CollapseAdd_Toggle_Hidden option"),
OBFUSCATE("ButtonLink_Source_https://github.com/LGLTeam"),
```

### JSON format

When a string starts with `{`, the parser treats it as a JSON object. Useful when the feature name contains underscores, commas, or quotes that would break the old parser.

| Key | Type | Notes |
|---|---|---|
| `type` | string | same names as above (`Toggle`, `SeekBar`, etc.) |
| `name` | string | label; use `text` for Category / RichTextView / RichWebView |
| `id` | number | optional, same numbering rules apply |
| `on` | bool | default state for Toggle / CheckBox / ButtonOnOff |
| `min` / `max` | number | SeekBar bounds |
| `items` | array or `"a,b"` string | Spinner / RadioButton choices |
| `url` | string | ButtonLink target |
| `collapseAdd` | bool | `true` → belongs to the current Collapse group |

```cpp
OBFUSCATE("{\"type\":\"Category\",\"text\":\"JSON section\"}"),
OBFUSCATE("{\"type\":\"Toggle\",\"id\":300,\"name\":\"Speed hack (name_with_underscores)\",\"on\":true}"),
OBFUSCATE("{\"type\":\"SeekBar\",\"name\":\"Speed\",\"min\":1,\"max\":200}"),
OBFUSCATE("{\"type\":\"Spinner\",\"name\":\"Quality\",\"items\":[\"Low\",\"Medium\",\"High\"]}"),
```

---

## Patch and hook macros

Defined in `Includes/Macros.h`. All string arguments are automatically obfuscated — no need to wrap them yourself.

### Hooks

```cpp
// Hook with original — call old_Func() inside your replacement
HOOK(lib, "0x123456", MyFunc, old_MyFunc);
HOOK(lib, "_SymbolName", MyFunc, old_MyFunc);

// Hook without original
HOOK_NO_ORIG(lib, "0x123456", MyFunc);

// Dobby instrument — log/count executions in logcat, no full hook
INST(lib, "0x123456", "tag", boolean);
```

`install_hook_name` (from `dobby.h`) is a shorthand that declares the original pointer and hook function together:

```cpp
install_hook_name(AddScore, void *, void *instance, int score) {
    return orig_AddScore(instance, score + scoreMul);
}
// In hack_thread:
install_hook_AddScore(getAbsoluteAddress(targetLibName, OBFUSCATE("0x107A2E0")));
```

### Patches

```cpp
// Apply — accepts raw hex bytes or ARM assembly
PATCH(lib, "0x123456", "C0 03 5F D6");
PATCH(lib, "_SymbolName", "ret");

// Switchable — apply or restore depending on bool
PATCH_SWITCH(lib, "0x123456", "C0 03 5F D6", boolean);

// Restore original bytes
RESTORE(lib, "0x123456");
```

Dynamic patches — printf-style arguments assembled into the instruction at runtime:

```cpp
dPATCH_SWITCH(boolean, lib, "0x123456", "mov w%d, #%d", 0, 1);
```

Relative patches — offset from a base symbol. Useful when a function moves between game versions but its internal layout is stable:

```cpp
rPATCH(lib, "0x1079728", "0x204", "C0 03 5F D6");
rPATCH_SWITCH(lib, "_sym", "0xAC", "ret", boolean);
```

The patch engine (`DobbyPatchWrapper`) reads and stores the original bytes the first time an address is patched, so `RESTORE` always has something to put back. Entries are keyed by `"libname:offset"` — two separate features can safely target the same address independently.

---

## Address resolution

`Utils.cpp` turns offsets and symbol names into absolute addresses using xDL.

```
getAbsoluteAddress(lib, "0x123456")  →  lib base + 0x123456
getAbsoluteAddress(lib, "_sym")      →  xdl_sym / xdl_dsym lookup
```

If the library isn't loaded yet, it returns `nullptr` — not a garbage low address. Callers (hooks, patches) skip the operation instead of crashing into unmapped memory.

`isLibraryLoaded(lib)` scans `/proc/self/maps` and sets the `mainLibLoaded` atomic flag as a side-effect. `IsGameLibLoaded()` (called from Java via JNI) reads that flag; `Launcher` uses it to decide whether hiding the overlay is appropriate.

---

## Native crash logger

Installed by `cl_install()` before `lib_main()` runs (`constructor(101)` fires before the default-priority constructor). Handles SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP on a dedicated alternate signal stack.

On crash, writes to:

```
/sdcard/Android/media/com.android.support/files/native_crash.log
```

The log contains: signal number, si_code, fault address, thread ID, PC, LR, and the full `/proc/self/maps`. Only async-signal-safe calls are used in the handler (open, write, read, close, strlen — no malloc, no stdio).

After writing, the handler resets the signal to `SIG_DFL` and re-raises it so the process terminates normally and the system crash dialog still appears.

---

## ESP overlay

A generic, engine-independent box / tracer / health / name ESP. It is split
into three layers so that targeting a new game touches exactly one of them.

```
ESPView.java            drawing layer   — transparent click-through overlay, Canvas
ESP/Esp.cpp             manager + JNI   — projects entities, packs a draw list
ESP/EspMath.hpp         projection      — worldToScreen, engine-agnostic
ESP/EntitySource.hpp    adapter API     — IEntitySource: matrix + entity list
ESP/SampleSource.cpp    adapter (stub)  — the one file you edit per game
```

### Data flow per frame

```
ESPView.doFrame (Choreographer, vsync)
  └─ onDraw
       ├─ nativeBuildFrame(w, h)        # Esp.cpp: ask the source, project, cull, pack
       │    ├─ IEntitySource::viewProjection()   → camera matrix
       │    ├─ IEntitySource::collect()          → entities (world coords)
       │    └─ worldToScreen() per entity        → screen pixels + cull
       ├─ nativeLabelText() / nativeLabelMeta()  # names / distances
       └─ Canvas draws boxes, tracers, bars, text
```

The View is a dumb renderer: it gets a flat `int[]` of shape records
(`[type, x1, y1, x2, y2, colorARGB]`) plus a parallel label list, and paints
them. All geometry and culling are native, so the same logic drives every game
and the overlay never changes.

### Projection conventions

`worldToScreen` is fixed maths; what differs per engine are *conventions*,
exposed as knobs on `ProjectionConfig`:

| Field | Meaning | Symptom if wrong |
|---|---|---|
| `columnMajor` | transpose the matrix before use (Unity is column-major) | ESP mirrored / transposed |
| `flipY` | NDC (+Y up) → Canvas (+Y down) | ESP upside-down |
| `minClipW` | cull points at/behind the camera plane | ghosts mirror from behind |

Getting these three to line up — not float precision — is what makes the boxes
lock on.

### Adding a game

Implement `IEntitySource` (copy `SampleSource.cpp`): fill `viewProjection()`
with the camera matrix and `collect()` with the entity list, then register it
(`esp::installSampleSource()` is called at the end of `hack_thread`). On Unity
the version-resilient route for the matrix is to call the engine's own
`Camera.WorldToScreenPoint` via il2cpp rather than chasing a matrix address.

### Menu controls

Feature IDs 500–509 in `GetFeatureList` (Main.cpp) toggle the ESP; `Changes()`
writes them into the atomic `esp::config()`. The explicit IDs keep these items
out of the automatic numbering used by the mod features above them.

---

## Adding a new feature

1. Add a string to `GetFeatureList` in `Main.cpp`:

```cpp
OBFUSCATE("Toggle_Infinite ammo"),
// or with an explicit ID:
OBFUSCATE("42_Toggle_Infinite ammo"),
```

2. Handle it in `Changes()`:

```cpp
case 42:
    PATCH_SWITCH(targetLibName, "0xABCDEF", "C0 03 5F D6", boolean);
    break;
```

   If you didn't assign an explicit ID, count the non-skipped items from 0 in order to find the right case number. `Category`, `ButtonLink`, `RichTextView`, and `RichWebView` don't count.

3. If the feature reads a value (SeekBar, Spinner, etc.), `Preferences.java` already saves and restores it. In `Changes()`, write the value to an `std::atomic` variable and read it inside your hook or `hack_thread`.

That's all. The menu rebuilds itself from the string array every time it's shown — no layout XML to edit.
