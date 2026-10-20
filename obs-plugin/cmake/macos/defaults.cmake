# CMake macOS defaults module

include_guard(GLOBAL)

# Set empty codesigning team if not specified as cache variable
if(NOT CODESIGN_TEAM)
  set(CODESIGN_TEAM "" CACHE STRING "OBS code signing team for macOS" FORCE)

  # Set ad-hoc codesigning identity if not specified as cache variable
  if(NOT CODESIGN_IDENTITY)
    set(CODESIGN_IDENTITY "-" CACHE STRING "OBS code signing identity for macOS" FORCE)
  endif()
endif()

include(xcode)

include(buildspec)

# Workaround: Qt's FindWrapOpenGL module always links AGL.framework on Apple platforms.
# Apple removed AGL from the macOS 26 SDK, but find_library() still resolves it from the
# running system under /System/Library/Frameworks. The linker only searches the active
# SDK, so the stale reference fails with "ld: framework 'AGL' not found".
#
# FindWrapOpenGL returns early when the imported target already exists, so defining it
# here - before any find_package(Qt6) call - keeps OpenGL.framework and drops AGL.
# The workaround disables itself automatically on SDKs that still ship AGL.
if(APPLE)
  set(_macos_sdk_path "${CMAKE_OSX_SYSROOT}")

  if(NOT IS_ABSOLUTE "${_macos_sdk_path}")
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE _macos_sdk_path
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()

  if(
    _macos_sdk_path
    AND NOT EXISTS "${_macos_sdk_path}/System/Library/Frameworks/AGL.framework"
    AND NOT TARGET WrapOpenGL::WrapOpenGL
  )
    message(STATUS "AGL.framework is absent from the macOS SDK - linking Qt against OpenGL only")
    add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED GLOBAL)
    target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE "-framework OpenGL")
  endif()

  unset(_macos_sdk_path)
endif()

# Use Applications directory as default install destination
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(
    CMAKE_INSTALL_PREFIX
    "$ENV{HOME}/Library/Application Support/obs-studio/plugins"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)
# Enable RPATH support for generated binaries
set(CMAKE_MACOSX_RPATH TRUE)
# Use RPATHs from build tree _in_ the build tree
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
# Do not add default linker search paths to RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
# Use common bundle-relative RPATH for installed targets
set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
