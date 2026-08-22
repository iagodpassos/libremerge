find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

qt_standard_project_setup()

qt_add_executable(LibreMerge
  main.cpp
  MainWindow.cpp
  FileCompareView.cpp
  DiffTextEdit.cpp
  LocationPane.cpp
  FolderCompareView.cpp
  FolderCompareDriver.cpp
  EngineOptions.cpp
)

set_target_properties(LibreMerge PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER org.libremerge.LibreMerge
  MACOSX_BUNDLE_BUNDLE_VERSION 0.1.0
  MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1.0
)

target_link_libraries(LibreMerge PRIVATE
  Qt6::Widgets
  Qt6::Concurrent
  libremerge-engine
)

if(NOT WIN32)
  target_compile_options(LibreMerge PRIVATE
    -include ${CMAKE_SOURCE_DIR}/engine/ports/posix_compat.h)
endif()
