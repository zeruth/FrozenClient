file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if(WHOA_SYSTEM_WIN)
    file(GLOB WIN_SOURCES CONFIGURE_DEPENDS
        "win/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${WIN_SOURCES})
endif()

if(WHOA_SYSTEM_MAC)
    file(GLOB MAC_SOURCES CONFIGURE_DEPENDS
        "mac/*.cpp"
        "mac/*.mm"
    )
    list(APPEND PRIVATE_SOURCES ${MAC_SOURCES})
endif()

if(WHOA_SYSTEM_ANDROID)
    file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS
        "android/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${ANDROID_SOURCES})
elseif(WHOA_SYSTEM_LINUX)
    file(GLOB LINUX_SOURCES CONFIGURE_DEPENDS
        "linux/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${LINUX_SOURCES})
endif()

add_library(event STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(event
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(event
    PRIVATE
        client
        gx
    PUBLIC
        common
        storm
        tempest
)

if(WHOA_SYSTEM_ANDROID)
    target_include_directories(event
        PRIVATE
            ${ANDROID_NDK}/sources/android/native_app_glue
    )

    target_link_libraries(event
        PRIVATE
            android
            log
    )
endif()
