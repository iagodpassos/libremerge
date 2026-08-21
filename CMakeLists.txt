cmake_minimum_required(VERSION 3.24)
project(libremerge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Homebrew keg-only ICU
if(APPLE AND EXISTS /opt/homebrew/opt/icu4c@78)
  list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/icu4c@78)
endif()

find_package(Poco REQUIRED COMPONENTS Foundation)
find_package(Boost REQUIRED CONFIG)
find_package(ICU REQUIRED COMPONENTS uc i18n)

enable_testing()
add_subdirectory(engine)
if(BUILD_TESTING)
  find_package(GTest CONFIG)
  if(GTest_FOUND)
    add_subdirectory(tests)
  endif()
endif()
