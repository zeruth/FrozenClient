plugins {
    id("com.android.application")
}

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

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}
