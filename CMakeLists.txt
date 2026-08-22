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

enable_testing()
add_subdirectory(engine)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  add_subdirectory(app)
else()
  message(STATUS "Qt6 not found - building the engine and tests only")
endif()

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
  FetchContent_MakeAvailable(googletest)
  if(NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
  endif()
endif()
add_subdirectory(tests)
