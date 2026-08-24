cmake_minimum_required(VERSION 3.24)
project(libremerge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Homebrew keg-only ICU
if(APPLE)
  foreach(_icu_prefix /opt/homebrew/opt/icu4c@78 /opt/homebrew/opt/icu4c /usr/local/opt/icu4c)
    if(EXISTS ${_icu_prefix})
      list(APPEND CMAKE_PREFIX_PATH ${_icu_prefix})
      break()
    endif()
  endforeach()
endif()

find_package(Poco REQUIRED COMPONENTS Foundation)
find_package(Boost REQUIRED CONFIG)
find_package(ICU REQUIRED COMPONENTS uc i18n)

# Qt (Homebrew keg hint)
if(APPLE AND EXISTS /opt/homebrew/opt/qt)
  list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/qt)
endif()

if(UNIX AND NOT APPLE)
  # `cpack -G DEB` in the build dir produces the .deb; runtime
  # dependencies are computed by dpkg-shlibdeps (needs dpkg-dev)
  set(CPACK_PACKAGE_NAME "libremerge")
  set(CPACK_PACKAGE_VERSION "0.7.3")
  set(CPACK_PACKAGE_CONTACT "Iago Passos <iago.dpassos@gmail.com>")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Iago Passos <iago.dpassos@gmail.com>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  # Qt platform plugins (wayland, xcb) are dlopen'd at runtime, which
  # dpkg-shlibdeps cannot see - without qt6-wayland the app aborts on a
  # GNOME Wayland session
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, qt6-wayland")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/iagodpassos/libremerge")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Diff and merge tool for files, folders and CSV tables")
  set(CPACK_PACKAGE_DESCRIPTION
    "A free, open-source differencing and merging tool: side-by-side file\ncomparison and merging, folder comparison, CSV/TSV table comparison,\nsyntax highlighting and 3-way merge, reusing the WinMerge engine with\na native Qt 6 interface.")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_STRIP_FILES ON)
  include(CPack)
endif()

enable_testing()
add_subdirectory(engine)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  add_subdirectory(app)
  add_subdirectory(packaging)
else()
  message(STATUS "Qt6 not found - building the engine and tests only")
endif()

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.zip)
  FetchContent_MakeAvailable(googletest)
  if(NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
  endif()
endif()
add_subdirectory(tests)
