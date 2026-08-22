find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

qt_standard_project_setup()

if(APPLE)
  set(LM_APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns)
  set_source_files_properties(${LM_APP_ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(LibreMerge
  ${LM_APP_ICON}
  main.cpp
  MainWindow.cpp
  NewComparisonView.cpp
  FileCompareView.cpp
  DiffTextEdit.cpp
  Icons.cpp
  LocationPane.cpp
  SyntaxHighlighter.cpp
  Theme.cpp
  FolderCompareView.cpp
  FolderCompareDriver.cpp
  FileOps.cpp
  EngineOptions.cpp
)

qt_add_resources(LibreMerge "i18n"
  PREFIX "/i18n"
  BASE i18n
  FILES i18n/libremerge_pt_BR.qm
)

set_target_properties(LibreMerge PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER org.libremerge.LibreMerge
  MACOSX_BUNDLE_BUNDLE_VERSION 0.4.1
  MACOSX_BUNDLE_SHORT_VERSION_STRING 0.4.1
  MACOSX_BUNDLE_ICON_FILE AppIcon.icns
  MACOSX_BUNDLE_BUNDLE_NAME LibreMerge
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in
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
