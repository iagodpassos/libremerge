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
  TableCompareView.cpp
  ImageCompareView.cpp
  ImagePane.cpp
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

if(APPLE)
  enable_language(OBJCXX)
  target_sources(LibreMerge PRIVATE MacServices.mm)
  target_link_libraries(LibreMerge PRIVATE "-framework AppKit")
  # localized titles for the Finder Services menu entry
  target_sources(LibreMerge PRIVATE
    ${CMAKE_SOURCE_DIR}/packaging/en.lproj/ServicesMenu.strings
    ${CMAKE_SOURCE_DIR}/packaging/pt.lproj/ServicesMenu.strings)
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/en.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/en.lproj")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/pt.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/pt.lproj")
endif()

set_target_properties(LibreMerge PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER org.libremerge.LibreMerge
  MACOSX_BUNDLE_BUNDLE_VERSION 0.7.3
  MACOSX_BUNDLE_SHORT_VERSION_STRING 0.7.3
  MACOSX_BUNDLE_ICON_FILE AppIcon.icns
  MACOSX_BUNDLE_BUNDLE_NAME LibreMerge
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in
)

target_link_libraries(LibreMerge PRIVATE
  Qt6::Widgets
  Qt6::Concurrent
  libremerge-engine
  libremerge-imgmerge
)

if(NOT WIN32)
  target_compile_options(LibreMerge PRIVATE
    -include ${CMAKE_SOURCE_DIR}/engine/ports/posix_compat.h)
endif()

if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  set_target_properties(LibreMerge PROPERTIES OUTPUT_NAME libremerge)
  install(TARGETS LibreMerge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/libremerge.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(_size 16 32 64 128 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/icons/libremerge-${_size}.png
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
      RENAME libremerge.png)
  endforeach()
endif()
