file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "guid/*.cpp"
    "time/*.cpp"
)

if(WHOA_SYSTEM_MAC)
    file(GLOB MAC_SOURCES CONFIGURE_DEPENDS "*.mm")

    set_source_files_properties(${MAC_SOURCES}
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    list(APPEND PRIVATE_SOURCES ${MAC_SOURCES})
endif()

if(WHOA_SYSTEM_WIN)
    file(GLOB WIN_SOURCES CONFIGURE_DEPENDS "win/*.cpp")

    list(APPEND PRIVATE_SOURCES ${WIN_SOURCES})
endif()

if(WHOA_SYSTEM_ANDROID)
    file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS "android/*.cpp")

    list(APPEND PRIVATE_SOURCES ${ANDROID_SOURCES})
endif()

add_library(util STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(util
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(util
    PUBLIC
        freetype-2.0
        lua-5.1
        common
        storm
        tempest
    PRIVATE
        stormlib-9.31
        zlib-1.2
)

if(WHOA_SYSTEM_LINUX OR WHOA_SYSTEM_MAC)
    target_link_libraries(util
        PRIVATE
            Threads::Threads
    )
endif()
