cmake_minimum_required(VERSION 3.16)
project(hullbreaker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_program(GLSLANG NAMES glslangValidator glslang)
if(NOT GLSLANG)
  message(FATAL_ERROR "glslangValidator not found (install glslang-tools / glslang)")
endif()

# ---- shaders: common.glsl is prepended to every stage, then compiled to a C header
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})
set(SHADERS sky.vert sky.frag mesh.vert mesh.frag particle.vert particle.frag ui.vert ui.frag)
set(SHADER_HEADERS)
foreach(S ${SHADERS})
  string(REPLACE "." "_" VAR ${S})
  add_custom_command(
    OUTPUT ${GEN_DIR}/${VAR}.h
    COMMAND ${CMAKE_COMMAND} -DCOMMON=${SHADER_DIR}/common.glsl -DSRC=${SHADER_DIR}/${S}
            -DOUT=${GEN_DIR}/${S} -P ${CMAKE_SOURCE_DIR}/cmake/concat.cmake
    COMMAND ${GLSLANG} -V --quiet --vn ${VAR}_spv -o ${GEN_DIR}/${VAR}.h ${GEN_DIR}/${S}
    DEPENDS ${SHADER_DIR}/common.glsl ${SHADER_DIR}/${S} ${CMAKE_SOURCE_DIR}/cmake/concat.cmake
    COMMENT "Compiling shader ${S}"
    VERBATIM)
  list(APPEND SHADER_HEADERS ${GEN_DIR}/${VAR}.h)
endforeach()

add_executable(hullbreaker
  src/main.cpp
  src/game.cpp
  src/renderer.cpp
  src/meshes.cpp
  src/ship.cpp
  src/world.cpp
  src/render_world.cpp
  src/ui.cpp
  src/selftest.cpp
  src/audio.cpp
  src/miniaudio_impl.cpp
  ${SHADER_HEADERS})
target_include_directories(hullbreaker PRIVATE ${GEN_DIR} src third_party)
find_package(Threads REQUIRED)
target_link_libraries(hullbreaker PRIVATE Vulkan::Vulkan glfw Threads::Threads ${CMAKE_DL_LIBS} m)
set_source_files_properties(src/miniaudio_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hullbreaker PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

install(TARGETS hullbreaker RUNTIME DESTINATION bin)
