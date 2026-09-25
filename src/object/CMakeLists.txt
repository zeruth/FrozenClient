file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "client/*.cpp"
    "movement/*.cpp"
)

add_library(object STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(object
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(object
    PRIVATE
        db
        ui
)
