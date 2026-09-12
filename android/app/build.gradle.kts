plugins {
    id("com.android.application")
}

// FMOD Engine for Android (not redistributable): vendor/fmodcore-2.02.18/android holds inc/,
// lib/<abi>/libfmod.so, and lib/fmod.jar when the user has downloaded it
val fmodAndroid = rootProject.file("../vendor/fmodcore-2.02.18/android")
val fmodJar = fmodAndroid.resolve("lib/fmod.jar")
logger.lifecycle("FMOD for Android at ${fmodAndroid}: ${if (fmodJar.exists()) "found" else "not found, sound is stubbed"}")

android {
    namespace = "com.frozenclient.app"
    compileSdk = 35
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.frozenclient.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        // The FMOD Java glue is the only Java code in the app
        manifestPlaceholders["hasCode"] = fmodJar.exists().toString()

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_PLATFORM=android-26")
                targets += listOf("Whoa")
            }
        }
    }

    externalNativeBuild {
        cmake {
            // The whole client tree; only the Whoa target is built for the app
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.srcDirs(fmodAndroid.resolve("lib"))

            // The bootstrap class references org.fmod.FMOD, so it only compiles with the jar
            if (!fmodJar.exists()) {
                java.setSrcDirs(emptyList<String>())
            }
        }
    }

    packaging {
        jniLibs {
            // The library is also an imported CMake target
            pickFirsts += listOf("**/libfmod.so", "**/libfmodL.so")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}

dependencies {
    if (fmodJar.exists()) {
        implementation(files(fmodJar))
    }
}
