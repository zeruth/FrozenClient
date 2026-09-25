file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "gui/*.cpp"
)

if(WHOA_SYSTEM_WIN)
    file(GLOB WIN_SOURCES CONFIGURE_DEPENDS
        "gui/win/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${WIN_SOURCES})
endif()

if(WHOA_SYSTEM_MAC)
    file(GLOB MAC_SOURCES CONFIGURE_DEPENDS
        "gui/mac/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${MAC_SOURCES})
endif()

if(WHOA_SYSTEM_ANDROID)
    file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS
        "gui/android/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${ANDROID_SOURCES})
elseif(WHOA_SYSTEM_LINUX)
    file(GLOB LINUX_SOURCES CONFIGURE_DEPENDS
        "gui/linux/*.cpp"
    )
    list(APPEND PRIVATE_SOURCES ${LINUX_SOURCES})
endif()

add_library(client STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(client
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(client
    PRIVATE
        ui
        async
        console
        db
        event
        glue
        gx
        model
        net
        util
        world
    PUBLIC
        bc
        common
        storm
)
