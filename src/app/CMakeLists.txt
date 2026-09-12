if(WHOA_SYSTEM_WIN)
    file(GLOB PRIVATE_SOURCES "win/*.cpp")

    add_executable(Whoa WIN32 ${PRIVATE_SOURCES})

    target_link_libraries(Whoa
        PRIVATE
            client
            event
            gx
            net
            util
        )
endif()

if(WHOA_SYSTEM_MAC)
    file(GLOB PRIVATE_SOURCES "mac/*.cpp" "mac/*.mm")

    set_source_files_properties(${PRIVATE_SOURCES}
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    add_executable(Whoa ${PRIVATE_SOURCES})

    target_link_libraries(Whoa
        PRIVATE
            client
            event
            gx
            net
            util
            "-framework AppKit"
            "-framework Carbon"
            "-framework IOKit"
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/mac/MainMenu.nib DESTINATION "bin")
endif()

if(WHOA_SYSTEM_ANDROID)
    file(GLOB PRIVATE_SOURCES "android/*.cpp")

    # The client is loaded by the native activity as a shared library, with the NDK's glue
    # providing the activity callbacks and the native thread
    list(APPEND PRIVATE_SOURCES ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

    add_library(Whoa SHARED ${PRIVATE_SOURCES})

    target_include_directories(Whoa
        PRIVATE
            ${ANDROID_NDK}/sources/android/native_app_glue
    )

    # Keep the glue's activity entry point even though nothing in the library references it
    target_link_options(Whoa PRIVATE -u ANativeActivity_onCreate)

    target_link_libraries(Whoa
        PRIVATE
            client
            event
            gx
            net
            util
            fmod
            android
            log
            EGL
            GLESv3
    )
elseif(WHOA_SYSTEM_LINUX)
    file(GLOB PRIVATE_SOURCES "linux/*.cpp")

    add_executable(Whoa ${PRIVATE_SOURCES})

    target_link_libraries(Whoa
        PRIVATE
            client
            event
            gx
            net
            util
    )
endif()

target_include_directories(Whoa
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

install(TARGETS Whoa DESTINATION "bin")

if(WHOA_SYSTEM_WIN)
    # Developer tool for listing and extracting client archive contents
    add_executable(MpqTool tools/MpqTool.cpp)

    target_include_directories(MpqTool
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(MpqTool
        PRIVATE
            client
            console
            util
            stormlib-9.31
    )

    # Developer tool for checking the ARB to GLSL ES translation used by the Android device
    add_executable(ArbTool tools/ArbTool.cpp ${CMAKE_SOURCE_DIR}/src/gx/gles/ArbToGlsl.cpp)

    target_include_directories(ArbTool
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
endif()
