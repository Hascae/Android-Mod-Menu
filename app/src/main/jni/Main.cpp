#include <list>
#include <vector>
#include <cstring>
#include <pthread.h>
#include <thread>
#include <atomic>
#include <cstring>
#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "ESP/Esp.hpp"
#include "ESP/EntitySource.hpp"
#include "dobby.h"

std::atomic<int> scoreMul{1}, coinsMul{1};

// Do not change or translate the first text unless you know what you are doing
// Assigning feature numbers is optional. Without it, it will automatically count for you, starting from 0
// Assigned feature numbers can be like any numbers 1,3,200,10... instead in order 0,1,2,3,4,5...
// ButtonLink, Category, RichTextView and RichWebView is not counted. They can't have feature number assigned
// Toggle, ButtonOnOff and Checkbox can be switched on by default, if you add True_. Example: CheckBox_True_The Check Box
// To learn HTML, go to this page: https://www.w3schools.com/

jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[] = {
            OBFUSCATE("Toggle_No death"),
            OBFUSCATE("Button_Start Invcibility (30 sec duration)"),
            OBFUSCATE("SeekBar_Score multiplier_1_100"),
            OBFUSCATE("SeekBar_Coins multiplier_1_1000"),
            OBFUSCATE("Category_Examples"), //Not counted
            OBFUSCATE("Toggle_The toggle"),
            OBFUSCATE(
                    "100_Toggle_True_The toggle 2"), //This one have feature number assigned, and switched on by default
            OBFUSCATE("110_Toggle_The toggle 3"), //This one too
            OBFUSCATE("SeekBar_The slider_1_100"),
            OBFUSCATE("SeekBar_Kittymemory slider example_1_5"),
            OBFUSCATE("Spinner_The spinner_Items 1,Items 2,Items 3"),
            OBFUSCATE("Button_The button"),
            OBFUSCATE("ButtonLink_The button with link_https://www.youtube.com/"), //Not counted
            OBFUSCATE("ButtonOnOff_The On/Off button"),
            OBFUSCATE("CheckBox_The Check Box"),
            OBFUSCATE("InputValue_Input number"),
            OBFUSCATE("InputValue_1000_Input number 2"), //Max value
			OBFUSCATE("1111_InputLValue_Input long number"),
            OBFUSCATE("InputLValue_1000000000000_Input long number 2"), //Max value
            OBFUSCATE("InputText_Input text"),
            OBFUSCATE("RadioButton_Radio buttons_OFF,Mod 1,Mod 2,Mod 3"),

            //Create new collapse
            OBFUSCATE("Collapse_Collapse 1"),
            OBFUSCATE("CollapseAdd_Toggle_The toggle"),
            OBFUSCATE("CollapseAdd_Toggle_The toggle"),
            OBFUSCATE("123_CollapseAdd_Toggle_The toggle"),
            OBFUSCATE("122_CollapseAdd_CheckBox_Check box"),
            OBFUSCATE("CollapseAdd_Button_The button"),

            //Create new collapse again
            OBFUSCATE("Collapse_Collapse 2_True"),
            OBFUSCATE("CollapseAdd_SeekBar_The slider_1_100"),
            OBFUSCATE("CollapseAdd_InputValue_Input number"),

            OBFUSCATE("RichTextView_This is text view, not fully HTML."
                      "<b>Bold</b> <i>italic</i> <u>underline</u>"
                      "<br />New line <font color='red'>Support colors</font>"
                      "<br/><big>bigger Text</big>"),
            OBFUSCATE("RichWebView_<html><head><style>body{color: white;}</style></head><body>"
                      "This is WebView, with REAL HTML support!"
                      "<div style=\"background-color: darkblue; text-align: center;\">Support CSS</div>"
                      "<marquee style=\"color: green; font-weight:bold;\" direction=\"left\" scrollamount=\"5\" behavior=\"scroll\">This is <u>scrollable</u> text</marquee>"
                      "</body></html>"),

            // ---- New JSON feature format ----
            // Every entry above is the classic underscore string and still works as-is. A feature
            // can instead be a JSON object, which lets the name hold '_' or other characters the
            // old format can't. Fields: type, name (or text), id (optional), on, min, max, items
            // (array or "a,b" string), url, collapseAdd.
            OBFUSCATE("{\"type\":\"Category\",\"text\":\"JSON format example\"}"),
            OBFUSCATE("{\"type\":\"Toggle\",\"id\":300,\"name\":\"JSON toggle (name_with_underscores)\",\"on\":true}"),

            // ---- ESP overlay ----
            // Generic, engine-independent box/tracer/health/name ESP. The drawing
            // and projection are wired up already; what each title still needs is a
            // data source (see ESP/SampleSource.cpp). Explicit IDs (500+) keep these
            // out of the automatic numbering used by the features above.
            OBFUSCATE("Category_ESP"),
            OBFUSCATE("500_Toggle_ESP enabled"),
            OBFUSCATE("501_Toggle_True_Boxes"),
            OBFUSCATE("502_Toggle_Corner box style"),
            OBFUSCATE("503_Toggle_Tracers"),
            OBFUSCATE("504_Toggle_True_Health bars"),
            OBFUSCATE("505_Toggle_True_Names"),
            OBFUSCATE("506_Toggle_Distance text"),
            OBFUSCATE("507_SeekBar_Max distance (0=off)_0_500"),
            OBFUSCATE("509_SeekBar_Line thickness_1_6"),
            OBFUSCATE("508_Spinner_Tracer origin_Bottom,Top,Center")
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    return (ret);
}

std::atomic<bool> btnPressed{false};

//Target main lib here
#define targetLibName OBFUSCATE("libil2cpp.so")

void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {

    switch (featNum) {
        case 0:
            // offset, hex
            PATCH_SWITCH(targetLibName, "0x1079728", "C0 03 5F D6", boolean);
            // The patch switch has been returned and reworked:
            // - (active) Dobby-Kitty implementation
            // - reworked KittyMemory implementation
            //
            // if you encounter any problems:
            // - switch to Kitty implementation (uncomment code in Macros.h)
            // - uncommiting logging for detailed debug
            // - special attention to the preferences -> this is the only source of this problem in the past that I have noticed:
            // -- try rename the preferences file;
            // -- for a maximum stable and flexible save settings, recommend using own system with XML/JSON files.

            // alt possibles usage variants:
            // symbol, hex
            PATCH_SWITCH(targetLibName, "_example__sym", "C0 03 5F D6", boolean);
            // offset, asm
            PATCH_SWITCH(targetLibName, "0x1079728", "ret", boolean);
            // symbol, asm
            PATCH_SWITCH(targetLibName, "_example__sym", "ret", boolean);

            // asm allows you to avoid using hex code, as it is generated automatically from the instructions.
            // - this is the awesome option if you know what you're doing
            // recommended insert ';' to separate statements, example: "mov x0, #1; ret"
            // recommended to test your instructions on https://armconverter.com or
            // https://shell-storm.org/online/Online-Assembler-and-Disassembler/

            // - this is probably especially useful with creating dynamic deep patches
            dPATCH_SWITCH(true, targetLibName, "0x1079728", "mov w%d, #%d", 0, 222);
            dPATCH_SWITCH(true, targetLibName, "_example__sym", "mov w%d, #%d", 0, 222);
            // standard formatting specifiers are supported (%d, %i, %x, %s, etc.)


            // Relative patches allow you to speed up patch creation if you are sure that the offsets within methods rarely change
            // So, you only need to update the offset instruction for the function
            // https://www.rapidtables.com/calc/math/hex-calculator.html <- use hex calculator to calculate the offset relative to the method
            // ! This is an extremely unstable due to the hard offsets... don't forget to check the logs to identify outdated offsets
            // offset, offset, hex
            rPATCH_SWITCH(targetLibName, "0x1079728", "0x204", "C0 03 5F D6", boolean);
            // sym, offset, hex
            rPATCH_SWITCH(targetLibName, "_example__sym", "0xAC", "C0 03 5F D6", boolean);
            // offset, offset, asm
            rPATCH_SWITCH(targetLibName, "0x1079728", "0x204", "mov x0, #0xffffff; ret", boolean);
            // sym, offset, asm
            rPATCH_SWITCH(targetLibName, "_example__sym", "0xAC", "mov x0, #0xffffff; ret", boolean);
            break;
        case 4:
            if(boolean) {
                // offset, hex
                PATCH(targetLibName, "0x10709AC", "E05F40B2 C0035FD6");
                rPATCH(targetLibName, "0x107094D", "0x5F", "E05F40B2 C0035FD6");

                // alt possibles usage variants:
                // symbol, hex
                PATCH(targetLibName, "_example__sym", "E0 5F 40 B2 C0 03 5F D6");
                rPATCH(targetLibName, "_example__sym", "0x5F", "E0 5F 40 B2 C0 03 5F D6");
                // offset, asm
                PATCH(targetLibName, "0x10709AC", "mov x0, #0xffffff; ret");
                rPATCH(targetLibName, "0x107094D", "0x5F", "mov x0, #0xffffff; ret");
                // symbol, asm
                PATCH(targetLibName, "_example__sym", "mov x0, #0xffffff; ret");
                rPATCH(targetLibName, "_example__sym", "0x5F", "mov x0, #0xffffff; ret");
            } else {
                RESTORE(targetLibName, "0x10709AC");
                rRESTORE(targetLibName, "0x10709AC", "0x5F");
                // or
                RESTORE(targetLibName, "_example__sym");
                rRESTORE(targetLibName, "_example__sym", "0x5F");
            }
            break;
        case 1:
            btnPressed = true;
            break;
        case 2:
            scoreMul = value;
            break;
        case 3:
            coinsMul = value;
            break;
        case 5:
            // you can use this for things as detect log, counting function calls, executing side code before the function is executed
            // now instrument wrapper implemented for detecting execution in logcat
            INST(targetLibName, "0x235630", "AnyNameForDetect2", boolean);

            if(boolean) {
                INST(targetLibName, "_example__sym", "AnyNameForDetect3", true);
            } else {
                INST(targetLibName, "_example__sym", "AnyNameForDetect3", false);
            }
            break;

        // ---- ESP overlay controls (IDs assigned in GetFeatureList) ----
        case 500:
            esp::config().enabled.store(boolean);
            break;
        case 501:
            esp::config().boxes.store(boolean);
            break;
        case 502:
            esp::config().cornerBox.store(boolean);
            break;
        case 503:
            esp::config().tracers.store(boolean);
            break;
        case 504:
            esp::config().healthBars.store(boolean);
            break;
        case 505:
            esp::config().names.store(boolean);
            break;
        case 506:
            esp::config().distances.store(boolean);
            break;
        case 507:
            esp::config().maxDistance.store(value);
            break;
        case 508:
            esp::config().tracerOrigin.store(value);
            break;
        case 509:
            esp::config().lineThickness.store(value);
            break;
        default:
            break;
    }
}

//CharacterPlayer
void (*StartInvcibility)(void *instance, float duration);

void (*old_Update)(void *instance);
void Update(void *instance) {
    if (instance != nullptr) {
        if (btnPressed) {
            StartInvcibility(instance, 30);
            btnPressed = false;
        }
    }
    return old_Update(instance);
}

/*
 void (*old_AddScore)(void *instance, int score);
 void AddScore(void *instance, int score) {
    //default any actions
    return old_AddScore(instance, score * scoreMul);
 }
*/
// === This function was completely replaced with `install_hook_name` from dobby.h ===
// (base name, return type, ... args)
install_hook_name(AddScore, void *, void *instance, int score) {
    // default any actions

    // use orig_ for call original function
    return orig_AddScore(instance, score + scoreMul);
}

void (*old_AddCoins)(void *instance, int count);
void AddCoins(void *instance, int count) {
    return old_AddCoins(instance, count * coinsMul);
}


// we will run our hacks in a new thread so our while loop doesn't block process main thread
void hack_thread() {
    // This loop should be always enabled in unity game
    // because libil2cpp.so is not loaded into memory immediately.
    while (!isLibraryLoaded(targetLibName)) {
        sleep(1); // Wait for target lib be loaded.
    }

    // In Android Studio, to switch between arm64-v8a and armeabi-v7a syntax highlighting,
    // You can modify the "Active ABI" in "Build Variants" to switch to another architecture for parsing.
#if defined(__aarch64__)
    //Il2Cpp: Use RVA offset
    StartInvcibility = (void (*)(void *, float)) getAbsoluteAddress(targetLibName, OBFUSCATE("0x107A3BC"));
    StartInvcibility = (void (*)(void *, float)) getAbsoluteAddress(targetLibName, OBFUSCATE("_characterPlayer_Update"));

    HOOK(targetLibName, "0x107A2FC", AddCoins, old_AddCoins);

    // HOOK(targetLibName, "0x107A2E0", AddScore, old_AddScore);
    // === This function was completely replaced with super-macro `install_hook_name` from dobby.h ===
    // don't forget set address for install_hook:
    // ! getAbsoluteAddress not have OBFUSCATE, so don't forget use his here
    install_hook_AddScore(getAbsoluteAddress(targetLibName,OBFUSCATE("0x107A2E0")));

    HOOK(targetLibName, "0x1078C44", Update, old_Update);
    //HOOK(targetLibName, "0x1079728", Kill, old_Kill);
    //HOOK(targetLibName, "_example__sym", Kill, old_Kill);
    //HOOK_NO_ORIG("libFileC.so", "0x123456", FunctionExample);
    //HOOK_NO_ORIG("libFileC.so", "_example__sym", FunctionExample);

    //PATCH(targetLibName, "0x10709AC", "E05F40B2C0035FD6");

    INST(targetLibName, "0x23558C", "AnyNameForDetect", true);

    // LOGI(OBFUSCATE("Test SYM: 0x%llx"), (uintptr_t)getAbsoluteAddress(OBFUSCATE("libil2cpp.so"), OBFUSCATE("il2cpp_init")));
#elif defined(__arm__)
    //Put your code here if you want the code to be compiled for armv7 only
#endif

    // Wire the ESP to its data source. The bundled template draws nothing until
    // you implement it (ESP/SampleSource.cpp); swap in your own IEntitySource
    // here once you've found the camera matrix and entity list for your game.
    esp::installSampleSource();

    LOGI(OBFUSCATE("Done"));
}

// Functions with `__attribute__((constructor))` are executed immediately when System.loadLibrary("lib_name") is called.
// If there are multiple such functions at the same time, `constructor(priority)` (the priority is an integer)
// will determine the execution priority, otherwise the execution order is undefined behavior.
// ===== Native crash logger (diagnostic, safe to remove once we're done) =====
// Native signals never reach the Java crash handler, so a crash in the .so leaves no log in
// crash_logs. This installs early (before lib_main) and dumps the signal, the faulting PC and
// /proc/self/maps to Android/media/<pkg>/files/native_crash.log so the faulting library can be
// pinned down. Only async-signal-safe calls are used in the handler.
static char cl_path[256];
static char cl_stack[64 * 1024];

static void cl_write(int fd, const char *s) { (void) write(fd, s, strlen(s)); }

static void cl_hex(int fd, unsigned long v) {
    char buf[2 + sizeof(unsigned long) * 2];
    char *p = buf + sizeof(buf);
    const char *h = "0123456789abcdef";
    if (v == 0) { *--p = '0'; } else { while (v) { *--p = h[v & 0xf]; v >>= 4; } }
    *--p = 'x';
    *--p = '0';
    (void) write(fd, p, (size_t) (buf + sizeof(buf) - p));
}

static void cl_dec(int fd, long v) {
    char buf[24];
    char *p = buf + sizeof(buf);
    bool neg = v < 0;
    unsigned long u = neg ? (unsigned long) (-(v + 1)) + 1u : (unsigned long) v;
    if (u == 0) { *--p = '0'; } else { while (u) { *--p = (char) ('0' + u % 10); u /= 10; } }
    if (neg) *--p = '-';
    (void) write(fd, p, (size_t) (buf + sizeof(buf) - p));
}

static void cl_handler(int sig, siginfo_t *info, void *ucontext) {
    int fd = open(cl_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        cl_write(fd, "=== NATIVE CRASH ===\nsignal=");
        cl_dec(fd, sig);
        cl_write(fd, " code=");
        cl_dec(fd, info->si_code);
        cl_write(fd, "\nfault_addr=");
        cl_hex(fd, (unsigned long) info->si_addr);
        cl_write(fd, "\ntid=");
        cl_dec(fd, (long) syscall(SYS_gettid));
        ucontext_t *uc = (ucontext_t *) ucontext;
#if defined(__aarch64__)
        cl_write(fd, "\npc=");
        cl_hex(fd, (unsigned long) uc->uc_mcontext.pc);
        cl_write(fd, "\nlr=");
        cl_hex(fd, (unsigned long) uc->uc_mcontext.regs[30]);
#elif defined(__arm__)
        cl_write(fd, "\npc=");
        cl_hex(fd, (unsigned long) uc->uc_mcontext.arm_pc);
        cl_write(fd, "\nlr=");
        cl_hex(fd, (unsigned long) uc->uc_mcontext.arm_lr);
#endif
        cl_write(fd, "\n=== /proc/self/maps ===\n");
        int mfd = open("/proc/self/maps", O_RDONLY);
        if (mfd >= 0) {
            char b[1024];
            ssize_t n;
            while ((n = read(mfd, b, sizeof(b))) > 0) (void) write(fd, b, (size_t) n);
            close(mfd);
        }
        cl_write(fd, "=== END ===\n");
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

__attribute__((constructor(101), used))
static void cl_install() {
    (void) mkdir("/sdcard/Android/media/com.android.support", 0777);
    (void) mkdir("/sdcard/Android/media/com.android.support/files", 0777);
    strcpy(cl_path, "/sdcard/Android/media/com.android.support/files/native_crash.log");

    stack_t ss;
    ss.ss_sp = cl_stack;
    ss.ss_size = sizeof(cl_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = cl_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    int sigs[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP};
    for (int i = 0; i < 6; i++) sigaction(sigs[i], &sa, nullptr);
}
// ===== end native crash logger =====

__attribute__((constructor))
void lib_main() {
    // Create a new thread so it does not block the main thread, means the game would not freeze
    // In modern C++, you should use std::thread(yourFunction).detach() instead of pthread_create
    std::thread(hack_thread).detach();
}