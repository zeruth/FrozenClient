#include <cstdint>

// Android entry point placeholder. The real entry runs the client from a GameActivity native
// thread; until that exists this keeps the client libraries linking as a shared library.
extern "C" int32_t WhoaAndroidMain(int32_t argc, char** argv) {
    // TODO CommonMain(argc, argv) once the Android platform layer exists

    return 0;
}
