cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

# The CI build scripts pass QT_VERSION; accept it so CMake does not warn that it went unused.
set(QT_VERSION 6 CACHE STRING "Qt version used by the plugin (only 6 is supported)")
mark_as_advanced(QT_VERSION)

if(ENABLE_QT)
  if(NOT QT_VERSION EQUAL 6)
    message(FATAL_ERROR "Atom requires Qt 6; QT_VERSION is set to ${QT_VERSION}.")
  endif()

  find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ATOM_ENABLE_QT)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# atom-core is deliberately free of OBS and Qt includes, which is what lets the designer preview
# run the exact same simulation as the rendered source.
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/atom-core/atom-config.cpp
    src/atom-core/atom-config.hpp
    src/atom-core/atom-fields.hpp
    src/atom-core/atom-math.hpp
    src/atom-core/atom-modulation.cpp
    src/atom-core/atom-modulation.hpp
    src/atom-core/atom-modules.cpp
    src/atom-core/atom-modules.hpp
    src/atom-core/atom-noise.hpp
    src/atom-core/atom-presets.cpp
    src/atom-core/atom-presets.hpp
    src/atom-core/atom-random.hpp
    src/atom-core/atom-registry.hpp
    src/atom-core/atom-system.cpp
    src/atom-core/atom-system.hpp
    src/atom-core/atom-types.hpp
    src/atom-core/atom-value.cpp
    src/atom-core/atom-value.hpp
    src/atom-core/atom-visual.cpp
    src/atom-core/atom-visual.hpp
    src/obs/atom-audio-meter.cpp
    src/obs/atom-audio-meter.hpp
    src/obs/atom-preset-store.cpp
    src/obs/atom-preset-store.hpp
    src/obs/atom-properties.cpp
    src/obs/atom-properties.hpp
    src/obs/atom-renderer.cpp
    src/obs/atom-renderer.hpp
    src/obs/atom-serialize.cpp
    src/obs/atom-serialize.hpp
    src/obs/atom-source.cpp
    src/obs/atom-source.hpp
    src/obs/atom-sprite-cache.cpp
    src/obs/atom-sprite-cache.hpp
    src/obs/atom-triggers.cpp
    src/obs/atom-triggers.hpp
    src/ui/atom-ui.cpp
    src/ui/atom-ui.hpp
)

if(ENABLE_QT)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/ui/atom-designer-dialog.cpp
      src/ui/atom-designer-dialog.hpp
      src/ui/atom-param-editor.cpp
      src/ui/atom-param-editor.hpp
      src/ui/atom-preview.cpp
      src/ui/atom-preview.hpp
      src/ui/atom-value-editors.cpp
      src/ui/atom-value-editors.hpp
  )
endif()

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

# atom-core has no OBS or Qt dependency, so it can be tested on its own.
option(ATOM_BUILD_TESTS "Build the atom-core test binary" OFF)

if(ATOM_BUILD_TESTS)
  enable_testing()
  add_executable(
    atom-core-tests
    tests/atom-core-tests.cpp
    src/atom-core/atom-config.cpp
    src/atom-core/atom-modulation.cpp
    src/atom-core/atom-modules.cpp
    src/atom-core/atom-presets.cpp
    src/atom-core/atom-system.cpp
    src/atom-core/atom-value.cpp
    src/atom-core/atom-visual.cpp
  )
  target_include_directories(atom-core-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_features(atom-core-tests PRIVATE cxx_std_17)
  add_test(NAME atom-core-tests COMMAND atom-core-tests)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
