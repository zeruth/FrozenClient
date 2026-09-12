package com.frozenclient.app;

import android.content.Context;

// The only Java in the app. FMOD's Android library caches the Java VM in its JNI_OnLoad, which
// the runtime only runs when Java loads the library, and System.loadLibrary needs a Java caller
// frame to pick the app's class loader, so the native activity calls in here through JNI.
public final class FmodBootstrap {
    private FmodBootstrap() {
    }

    public static void init(Context context) {
        System.loadLibrary("fmod");
        org.fmod.FMOD.init(context);
    }

    public static void close() {
        org.fmod.FMOD.close();
    }
}
