#include "client/Client.hpp"
#include "event/Input.hpp"
#include "util/android/OsAndroid.hpp"
#include <android_native_app_glue.h>
#include <android/log.h>
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

    CommonMain();

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Client main returned");
}
