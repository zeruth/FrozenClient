#include "client/Client.hpp"
#include "event/Input.hpp"
#include "util/android/OsAndroid.hpp"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#define LOG_TAG "Whoa"

// The client prints Lua errors and assertions to stdout and stderr; on Android those go nowhere
// unless they are forwarded to the log
static void* LogPumpThread(void* param) {
    auto fd = static_cast<int>(reinterpret_cast<intptr_t>(param));
    char line[1024];
    size_t used = 0;

    while (true) {
        auto read = ::read(fd, line + used, sizeof(line) - 1 - used);

        if (read <= 0) {
            break;
        }

        used += read;
        line[used] = 0;

        char* start = line;
        char* newline;

        while ((newline = strchr(start, 10)) != nullptr) {
            *newline = 0;
            __android_log_write(ANDROID_LOG_INFO, LOG_TAG, start);
            start = newline + 1;
        }

        used = strlen(start);
        memmove(line, start, used + 1);

        if (used == sizeof(line) - 1) {
            __android_log_write(ANDROID_LOG_INFO, LOG_TAG, line);
            used = 0;
        }
    }

    return nullptr;
}

static void RedirectOutputToLog() {
    int fds[2];

    if (pipe(fds) != 0) {
        return;
    }

    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);

    pthread_t thread;
    pthread_create(&thread, nullptr, LogPumpThread, reinterpret_cast<void*>(static_cast<intptr_t>(fds[0])));
    pthread_detach(thread);
}

// FMOD's Android build needs org.fmod.FMOD.init(context) before the system is created and
// close() when done. The class lives in fmod.jar, which the activity's class loader can find
// even though this thread was never attached to the VM.
static void FmodJavaCall(android_app* app, const char* method) {
#if defined(WHOA_FMOD_ANDROID)
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;

    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Cannot attach to the Java VM for FMOD.%s", method);
        return;
    }

    jobject activity = app->activity->clazz;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getClassLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallObjectMethod(activity, getClassLoader);
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring className = env->NewStringUTF("org.fmod.FMOD");
    auto fmodClass = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, className));

    if (env->ExceptionCheck() || !fmodClass) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "org.fmod.FMOD is not in the APK; is fmod.jar vendored?");
    } else if (!strcmp(method, "init")) {
        jmethodID init = env->GetStaticMethodID(fmodClass, "init", "(Landroid/content/Context;)V");
        env->CallStaticVoidMethod(fmodClass, init, activity);
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "FMOD Java glue initialized");
    } else {
        jmethodID close = env->GetStaticMethodID(fmodClass, "close", "()V");
        env->CallStaticVoidMethod(fmodClass, close);
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    vm->DetachCurrentThread();
#endif
}

// Native activity entry point. The activity thread waits for its window, moves into the app's
// external files directory (where Data\ and WTF\ live, exactly like the desktop layout), and then
// runs the same client main as the other platforms.
void android_main(android_app* app) {
    RedirectOutputToLog();

    OsAndroidSetApp(app);

    app->onAppCmd = OsAndroidOnAppCommand;
    app->onInputEvent = OsAndroidOnInputEvent;

    // The graphics device needs the window, so wait for the activity to provide it
    while (!OsAndroidGetWindow() && !app->destroyRequested) {
        OsAndroidPollEvents(-1);
    }

    if (app->destroyRequested) {
        return;
    }

    const char* dataDir = app->activity->externalDataPath;

    if (dataDir) {
        mkdir(dataDir, 0755);

        if (chdir(dataDir) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Cannot change to data directory %s", dataDir);
        } else {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Running from %s", dataDir);
        }
    }

    FmodJavaCall(app, "init");

    CommonMain();

    FmodJavaCall(app, "close");

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Client main returned");
}
