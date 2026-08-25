# Upstream GoogleTest suite for the engine, running on the ported library.
# Windows-only suites (RegOptionsMgr, mystat) are excluded on POSIX.

set(TEST_SOURCES
  support/test_main.cpp
  BinaryCompare/BinaryCompare_test.cpp
  ByteCompare/ByteCompare_test.cpp
  DIffItemList/DiffItemList_test.cpp
  DiffCode/DiffCode_test.cpp
  DiffWrapper/DiffWrapper_test.cpp
  Encoding/charsets_test.cpp
  Encoding/codepage_detect_test.cpp
  ExistenceCompare/ExistenceCompare_test.cpp
  FileFilter/FileFilterHelper_test.cpp
  FilterEngine/FilterExpression_test.cpp
  FullQuickCompare/FullQuickCompare_test.cpp
  MoveDetection/RenameMoveDetection_test.cpp
  OptionsMgr/VariantValue_test.cpp
  StringDiffs/stringdiffs_test.cpp
  StringDiffs/stringdiffs_test_adds.cpp
  StringDiffs/stringdiffs_test_bugs.cpp
  StringDiffs/stringdiffs_test_bytelevel.cpp
  SubstitutionList/SubstitutionList_test.cpp
  TimeSizeCompare/TimeSizeCompare_test.cpp
  UnicodeString/UnicodeString_test.cpp
  diffutils/util_test.cpp
  unicoder/unicoder_test.cpp
  xdiff/xutils_test.cpp
)

if(WIN32)
  # upstream test asserts Windows path semantics (drive letters, backslash)
  list(APPEND TEST_SOURCES Paths/paths_test.cpp)
else()
  list(APPEND TEST_SOURCES Paths/paths_posix_test.cpp)
endif()

add_executable(libremerge-tests ${TEST_SOURCES})

# Fixtures resolved relative to the test binary (env::GetProgPath())
add_custom_command(TARGET libremerge-tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/TestData $<TARGET_FILE_DIR:libremerge-tests>/../TestData
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/FileFilter/Filters $<TARGET_FILE_DIR:libremerge-tests>/../FileFilter/Filters
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/Data $<TARGET_FILE_DIR:libremerge-tests>/../../Data
)

target_link_libraries(libremerge-tests PRIVATE
  libremerge-engine
  GTest::gtest
)

if(NOT WIN32)
  target_compile_options(libremerge-tests PRIVATE
    -include ${CMAKE_SOURCE_DIR}/engine/ports/posix_compat.h)
endif()

# Image-compare core tests (needs Qt6 Gui; own binary with a
# QGuiApplication offscreen main)
include(GoogleTest)
if(TARGET libremerge-imgmerge)
  add_executable(libremerge-imgmerge-tests ImgMerge/ImgMergeBuffer_test.cpp)
  target_link_libraries(libremerge-imgmerge-tests PRIVATE
    libremerge-imgmerge
    GTest::gtest
  )
  gtest_discover_tests(libremerge-imgmerge-tests DISCOVERY_TIMEOUT 60)
endif()

# Tests use upstream-relative data paths (../TestData, ../../Data,
# ../../Src, ...). tests/run plus the Src/Externals symlinks at the repo
# root reproduce that layout.
include(GoogleTest)
gtest_discover_tests(libremerge-tests
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/gtest/run
  DISCOVERY_TIMEOUT 60
)
