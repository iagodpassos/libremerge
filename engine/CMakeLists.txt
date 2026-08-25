# libremerge-engine: the WinMerge comparison engine as a portable library.
# Built in layers, innermost first:
#   L0  diffutils + xdiff        (C diff cores)
#   L1  Src/Common portable set  (strings, unicode, files, options)
#   L2  engine orchestration     (DiffWrapper, DirScan, filters, ...)

set(L0_SOURCES
  Src/diffutils/GnuVersion.c
  Src/diffutils/lib/cmpbuf.c
  Src/diffutils/src/analyze.c
  Src/diffutils/src/context.c
  Src/diffutils/src/ed.c
  Src/diffutils/src/ifdef.c
  Src/diffutils/src/io.c
  Src/diffutils/src/normal.c
  Src/diffutils/src/side.c
  Src/diffutils/src/util.c
  Src/diffutils/src/Diff.cpp
  Externals/xdiff/xdiffi.c
  Externals/xdiff/xemit.c
  Externals/xdiff/xhistogram.c
  Externals/xdiff/xmerge.c
  Externals/xdiff/xnone.c
  Externals/xdiff/xpatience.c
  Externals/xdiff/xprepare.c
  Externals/xdiff/xutils.c
)

if(WIN32)
  list(APPEND L0_SOURCES Src/diffutils/src/mystat.cpp)
endif()

# L1/L2 are appended as they are ported (see git history).
set(L1_SOURCES
  Src/Common/UnicodeString.cpp
  Src/Common/charsets.c
  Src/Common/coretools.cpp
  Src/Common/varprop.cpp
  Src/Common/PathContext.cpp
  Src/Common/OptionsMgr.cpp
  Src/Logger.cpp
  Src/Common/unicoder.cpp
  Src/Common/UniFile.cpp
  Src/Common/cio.cpp
  Src/codepage_detect.cpp
  Src/markdown.cpp
  Src/TempFile.cpp
  ports/paths_posix.cpp
  ports/environment_posix.cpp
  ports/win_codepage.cpp
  ports/normalize_icu.cpp
  ports/win_ini.cpp
  ports/options_global.cpp
  ports/locality_posix.cpp
)
set(L2_SOURCES
  Src/CompareOptions.cpp
  Src/CompareStats.cpp
  Src/DiffWrapper.cpp
  Src/DiffList.cpp
  Src/DiffFileData.cpp
  Src/DiffFileInfo.cpp
  Src/DiffItem.cpp
  Src/DiffItemList.cpp
  Src/DirItem.cpp
  Src/DirScan.cpp
  Src/DirTravel.cpp
  Src/DiffContext.cpp
  Src/FolderCmp.cpp
  Src/MovedBlocks.cpp
  Src/MovedLines.cpp
  Src/stringdiffs.cpp
  Src/xdiff_gnudiff_compat.cpp
  Src/FileFilter.cpp
  Src/FileFilterHelper.cpp
  Src/FileFilterMgr.cpp
  Src/FilterList.cpp
  Src/FilterErrorMessages.cpp
  Src/LineFiltersList.cpp
  Src/SubstitutionList.cpp
  Src/SubstitutionFiltersList.cpp
  Src/CompareEngines/ByteCompare.cpp
  Src/CompareEngines/ByteComparator.cpp
  Src/CompareEngines/BinaryCompare.cpp
  Src/CompareEngines/ExistenceCompare.cpp
  Src/CompareEngines/FullQuickCompare.cpp
  Src/CompareEngines/TimeSizeCompare.cpp
  Src/CompareEngines/Wrap_DiffUtils.cpp
  Src/FilterEngine/FileContentRef.cpp
  Src/FilterEngine/FilterExpression.cpp
  Src/FilterEngine/FilterExpressionNodes.cpp
  Src/FilterEngine/FilterParser.cpp
  Src/FilterEngine/FolderStats.cpp
  Src/FilterEngine/ReplaceList.cpp
  Src/RenameMoveDetection.cpp
  Src/SyntaxParserHelper.cpp
  Src/PatchHTML.cpp
  Src/FileVersion.cpp
  Src/FileTextEncoding.cpp
  Src/FileFlags.cpp
  ports/debug_new.cpp
  Externals/crystaledit/editlib/utils/string_util.cpp
  Externals/crystaledit/editlib/utils/fpattern.cpp
  Externals/crystaledit/editlib/utils/icu.cpp
  Externals/crystaledit/editlib/TextDefinition.cpp
  ports/exconverter_icu.cpp
  ports/file_transform_posix.cpp
  ports/image_compare_stub.cpp
  ports/property_system_posix.cpp
  ports/merge_app_posix.cpp
)
if(WIN32)
  list(APPEND L2_SOURCES Src/FilterEngine/FunctionNodeProperty.cpp)
endif()

# FilterEngine generated sources: lexer via re2c, parser via lemon
find_program(RE2C_EXECUTABLE re2c REQUIRED)
find_program(LEMON_EXECUTABLE lemon REQUIRED)
find_file(LEMON_TEMPLATE lempar.c PATHS
  /opt/homebrew/opt/lemon/share/lemon /usr/share/lemon /usr/local/share/lemon)
set(FILTER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/filtergen)
file(MAKE_DIRECTORY ${FILTER_GEN_DIR})
add_custom_command(
  OUTPUT ${FILTER_GEN_DIR}/FilterLexer.cpp
  COMMAND ${RE2C_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Src/FilterEngine/FilterLexer.re
          -o ${FILTER_GEN_DIR}/FilterLexer.cpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Src/FilterEngine/FilterLexer.re
  COMMENT "re2c: FilterLexer.re -> FilterLexer.cpp")
if(LEMON_TEMPLATE)
  set(LEMON_TEMPLATE_ARG -T${LEMON_TEMPLATE})
endif()
add_custom_command(
  OUTPUT ${FILTER_GEN_DIR}/FilterParser.c
  COMMAND ${CMAKE_COMMAND} -E copy
          ${CMAKE_CURRENT_SOURCE_DIR}/Src/FilterEngine/FilterParser.y ${FILTER_GEN_DIR}/FilterParser.y
  COMMAND ${LEMON_EXECUTABLE} ${LEMON_TEMPLATE_ARG} ${FILTER_GEN_DIR}/FilterParser.y
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Src/FilterEngine/FilterParser.y
  COMMENT "lemon: FilterParser.y -> FilterParser.c")
file(GLOB CRYSTAL_PARSER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/Externals/crystaledit/editlib/parsers/*.cpp)
list(APPEND L2_SOURCES
  ${FILTER_GEN_DIR}/FilterLexer.cpp
  Externals/crystaledit/editlib/SyntaxParserRegistry.cpp
  Externals/crystaledit/editlib/CrystalLineSyntaxParser.cpp
  ${CRYSTAL_PARSER_SOURCES}
)
set_source_files_properties(Src/FilterEngine/FilterParser.cpp
  PROPERTIES OBJECT_DEPENDS ${FILTER_GEN_DIR}/FilterParser.c)

add_library(libremerge-engine STATIC
  ${L0_SOURCES}
  ${L1_SOURCES}
  ${L2_SOURCES}
)

target_include_directories(libremerge-engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/Common
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/diffutils
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/diffutils/src
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/diffutils/lib
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/FilterEngine
  ${CMAKE_CURRENT_SOURCE_DIR}/Src/CompareEngines
  ${FILTER_GEN_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Externals
  ${CMAKE_CURRENT_SOURCE_DIR}/Externals/crystaledit/editlib
  ${CMAKE_CURRENT_SOURCE_DIR}/ports
)

if(NOT WIN32)
  target_compile_options(libremerge-engine PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/ports/posix_compat.h)
endif()

target_link_libraries(libremerge-engine PUBLIC
  Poco::Foundation
  Boost::boost
  ICU::uc ICU::i18n
)

# Image-compare core: the vendored WinIMerge algorithm headers
# (Externals/winimerge) over a Qt-backed image wrapper (ports/imgmerge).
# Header-only; consumers need Qt6 Gui only, so the target exists whenever
# Qt6 is present even in engine-only builds.
find_package(Qt6 COMPONENTS Gui QUIET)
if(Qt6Gui_FOUND)
  add_library(libremerge-imgmerge INTERFACE)
  target_include_directories(libremerge-imgmerge INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/imgmerge
    ${CMAKE_CURRENT_SOURCE_DIR}/Externals/winimerge/src/WinIMergeLib
  )
  target_link_libraries(libremerge-imgmerge INTERFACE Qt6::Gui)
endif()
