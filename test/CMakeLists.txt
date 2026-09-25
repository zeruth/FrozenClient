if(WHOA_SYSTEM_MAC)
    file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS
        "Test.cpp"
        "stub/Mac.mm"
        "console/*.cpp"
        "gx/*.cpp"
        "gx/font/*.cpp"
        "util/*.cpp"
        "util/time/*.cpp"
    )

    set_source_files_properties(${PRIVATE_SOURCES}
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    add_executable(FrozenTest ${PRIVATE_SOURCES})

    target_link_libraries(FrozenTest
        PRIVATE
            client
            event
            gx
            util
            "-framework AppKit"
            "-framework Carbon"
            "-framework IOKit"
    )
endif()

if(WHOA_SYSTEM_WIN OR WHOA_SYSTEM_LINUX)
    file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS
        "Test.cpp"
        "console/*.cpp"
        "gx/*.cpp"
        "gx/font/*.cpp"
        "util/*.cpp"
    )

    add_executable(FrozenTest ${PRIVATE_SOURCES})

    target_link_libraries(FrozenTest
        PRIVATE
            client
            event
            gx
            util
            stormlib-9.31
    )
endif()

target_include_directories(FrozenTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/vendor/catch2-2.13.10
)

install(TARGETS FrozenTest DESTINATION "bin")

if(WHOA_SYSTEM_ANDROID)
    # Catch2 writes through the Android log
    target_link_libraries(FrozenTest PRIVATE log)
endif()
