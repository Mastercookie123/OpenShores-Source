au_add_static_library(AuGL "${CMAKE_CURRENT_SOURCE_DIR}" DEFINE AUGL)
target_link_libraries(AuGL PUBLIC AuUtil AuMath)

# The compatibility bridge performs per-frame geometry/parameter collection.
# Keep symbols in debug clients, but avoid shipping its hot loops at -O0.
if(MINGW)
    set_source_files_properties(aufilamentworld.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
