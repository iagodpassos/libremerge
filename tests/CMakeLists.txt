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
  Paths/paths_test.cpp
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

add_executable(libremerge-tests ${TEST_SOURCES})

target_link_libraries(libremerge-tests PRIVATE
  libremerge-engine
  GTest::gtest
)

if(NOT WIN32)
  target_compile_options(libremerge-tests PRIVATE
    -include ${CMAKE_SOURCE_DIR}/engine/ports/posix_compat.h)
endif()

# Tests reference TestData/ relative to their own directory layout; run them
# from the tests source dir so those relative paths resolve.
include(GoogleTest)
gtest_discover_tests(libremerge-tests
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DISCOVERY_TIMEOUT 60
)
