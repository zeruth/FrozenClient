file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "map/*.cpp"
)

add_library(world STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(world
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(world
    PRIVATE
        gx
        model
        # FMOD headers only: CMapDoodadDef embeds a SOUNDKITOBJECT, whose SESound pulls in
        # fmod.hpp. The sound library links ui, which links world, so world cannot link sound
        # back; SI2's symbols resolve when Frozen links both.
        fmod
)
