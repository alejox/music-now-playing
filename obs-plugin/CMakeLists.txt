cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# La fuente, el dock y el lector usan la API del frontend de OBS y Qt.
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

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/app-volume.cpp
    src/app-volume.hpp
    src/media-watcher.cpp
    src/media-watcher.hpp
    src/music-channel.cpp
    src/music-channel.hpp
    src/now-playing-source.cpp
    src/now-playing-source.hpp
    src/now-playing-dock.cpp
    src/now-playing-dock.hpp
    src/updater.cpp
    src/updater.hpp
    src/release-signature.cpp
    src/release-signature.hpp
    src/third-party/monocypher/monocypher.c
    src/third-party/monocypher/monocypher.h
    src/third-party/monocypher/monocypher-ed25519.c
    src/third-party/monocypher/monocypher-ed25519.h
)

# macOS: captura propia del audio de una app para el canal de música (ScreenCaptureKit, macOS 13+).
# El destino mínimo es macOS 12.0 y ScreenCaptureKit existe desde 12.3: se enlaza débil y el código
# lo usa solo tras comprobar @available(macOS 13.0, *). Xcode compila el .mm con ARC (xcode.cmake).
if(APPLE)
  enable_language(OBJCXX)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/app-audio-capture-mac.mm src/app-audio-capture.hpp)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      "$<LINK_LIBRARY:WEAK_FRAMEWORK,ScreenCaptureKit>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreMedia>"
      "$<LINK_LIBRARY:FRAMEWORK,AppKit>"
      "$<LINK_LIBRARY:FRAMEWORK,CoreGraphics>"
  )
endif()

# Monocypher es código de terceros: en MSVC sus conversiones de enteros (intencionales en criptografía)
# dan avisos que /WX vuelve errores. Se silencian solo en esos dos archivos.
if(MSVC)
  set_source_files_properties(
    src/third-party/monocypher/monocypher.c
    src/third-party/monocypher/monocypher-ed25519.c
    PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4146;/wd4267"
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
