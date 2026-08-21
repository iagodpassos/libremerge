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
set(L1_SOURCES)
set(L2_SOURCES)

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
