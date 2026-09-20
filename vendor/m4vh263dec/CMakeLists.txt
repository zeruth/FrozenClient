file(GLOB M4VH263DEC_SOURCES "src/*.cpp")

add_library(m4vh263dec STATIC ${M4VH263DEC_SOURCES})

target_include_directories(m4vh263dec
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        # <log/log.h> stub, so the AOSP sources build unmodified off-Android
        "${CMAKE_CURRENT_SOURCE_DIR}/compat"
)

# OSCL_IMPORT_REF / OSCL_EXPORT_REF are Android build decorations; the decoder is linked straight
# into Frozen, so they are empty here.
target_compile_definitions(m4vh263dec PUBLIC OSCL_IMPORT_REF= OSCL_EXPORT_REF=)

# Vendored third-party code: do not fail the build on its warnings, and keep it out of ours.
if(MSVC)
    target_compile_options(m4vh263dec PRIVATE /W0)
    # The sources carry GCC/Clang __attribute__ spellings MSVC does not parse; see msvc_shim.h.
    target_compile_options(m4vh263dec PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/msvc_shim.h")
else()
    target_compile_options(m4vh263dec PRIVATE -w)
endif()

set_target_properties(m4vh263dec PROPERTIES FOLDER "vendor")
