file(GLOB PRIVATE_SOURCES
    "*.cpp"
    "game/*.cpp"
    "simple/*.cpp"
)

add_library(ui STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(ui
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ui
    PRIVATE
        client
        console
        db
        event
        gx
        math
        model
        net
        object
        util
        # MPEG-4 decoder for the cinematics (vendor/m4vh263dec)
        m4vh263dec
    PUBLIC
        common
        storm
        tempest
)
