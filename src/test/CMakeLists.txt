add_executable(RunTests)
target_sources(RunTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp)
target_link_libraries(RunTests PRIVATE doctest AuUtil AuMath)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/**/*.h"
)

target_sources(RunTests PRIVATE ${sources})

# Requires a working desktop OpenGL context; run explicitly on GPU-capable hosts.
add_executable(RenderTextureProbe EXCLUDE_FROM_ALL texture_filter_probe.cc)
target_link_libraries(RenderTextureProbe PRIVATE AuGL AuPlatform)

add_executable(RenderCharacterProbe EXCLUDE_FROM_ALL character_probe.cc)
target_link_libraries(RenderCharacterProbe PRIVATE AuData AuPlatform)

add_executable(ExportHumanReference EXCLUDE_FROM_ALL human_reference.cc)
target_link_libraries(ExportHumanReference PRIVATE AuData AuPlatform AuBodyPartsRes)

add_executable(ValidateProceduralParts EXCLUDE_FROM_ALL procedural_parts.cc)
target_link_libraries(ValidateProceduralParts PRIVATE AuData AuPlatform AuBodyPartsRes)

add_executable(RenderProceduralAvatar EXCLUDE_FROM_ALL procedural_avatar.cc)
target_link_libraries(RenderProceduralAvatar PRIVATE AuData AuPlatform AuBodyPartsRes)

add_executable(ExportRendererScene EXCLUDE_FROM_ALL renderer_scene.cc)
target_link_libraries(ExportRendererScene PRIVATE AuData AuPlatform AuBodyPartsRes)
