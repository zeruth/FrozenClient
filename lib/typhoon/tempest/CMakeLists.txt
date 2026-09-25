file(GLOB TEMPEST_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "box/*.cpp"
    "math/*.cpp"
    "matrix/*.cpp"
    "quaternion/*.cpp"
    "random/*.cpp"
    "rect/*.cpp"
    "segment/*.cpp"
    "vector/*.cpp"
)

add_library(tempest STATIC
    ${TEMPEST_SOURCES}
)

target_include_directories(tempest
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(tempest
    PUBLIC
        storm
)
