find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
find_package(LibArchive REQUIRED)

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
  MessageBoxes.cpp
  NewComparisonView.cpp
  FileCompareView.cpp
  TableCompareView.cpp
  ImageCompareView.cpp
  ImagePane.cpp
  OptionsDialog.cpp
  FiltersDialog.cpp
  FileFilterCombo.cpp
  FileFilterMenu.cpp
  FilterConditionDialog.cpp
  ComparisonResultFilterDialog.cpp
  DisplayFilterBar.cpp
  LineFilterMenu.cpp
  MatchInsideDialog.cpp
  ReplaceLists.cpp
  FileFilters.cpp
  DiffTextEdit.cpp
  Icons.cpp
  ItemCheckStyle.cpp
  LocationPane.cpp
  SyntaxHighlighter.cpp
  Theme.cpp
  FolderCompareView.cpp
  FolderCompareDriver.cpp
  ArchiveCompare.cpp
  AboutDialog.cpp
  ProjectLinks.cpp
  StarPrompt.cpp
  DesktopIntegration.cpp
  FileOps.cpp
  EngineOptions.cpp
)

qt_add_resources(LibreMerge "i18n"
  PREFIX "/i18n"
  BASE i18n
  FILES i18n/libremerge_ca.qm i18n/libremerge_de.qm i18n/libremerge_es.qm
    i18n/libremerge_pt_BR.qm
)

# About box: the contributors list (shown by the Contributors button) and
# the application icon for the banner, both from outside app/
set(LM_ABOUT_CONTRIBUTORS ${CMAKE_SOURCE_DIR}/CONTRIBUTORS.md)
set(LM_ABOUT_ICON ${CMAKE_SOURCE_DIR}/packaging/linux/icons/libremerge-512.png)
set_source_files_properties(${LM_ABOUT_CONTRIBUTORS}
  PROPERTIES QT_RESOURCE_ALIAS CONTRIBUTORS.md)
set_source_files_properties(${LM_ABOUT_ICON}
  PROPERTIES QT_RESOURCE_ALIAS icon.png)
qt_add_resources(LibreMerge "about"
  PREFIX "/about"
  FILES ${LM_ABOUT_CONTRIBUTORS} ${LM_ABOUT_ICON}
)

# Linux desktop integration (--install-desktop-integration): the packaged
# desktop entry, retargeted at the AppImage, and the hicolor icons
set(LM_DESKTOP_FILES ${CMAKE_SOURCE_DIR}/packaging/linux/libremerge.desktop)
set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/linux/libremerge.desktop
  PROPERTIES QT_RESOURCE_ALIAS libremerge.desktop)
foreach(_size 16 32 64 128 256 512)
  set(_icon ${CMAKE_SOURCE_DIR}/packaging/linux/icons/libremerge-${_size}.png)
  set_source_files_properties(${_icon}
    PROPERTIES QT_RESOURCE_ALIAS icons/libremerge-${_size}.png)
  list(APPEND LM_DESKTOP_FILES ${_icon})
endforeach()
qt_add_resources(LibreMerge "desktop"
  PREFIX "/desktop"
  FILES ${LM_DESKTOP_FILES}
)

if(APPLE)
  enable_language(OBJCXX)
  target_sources(LibreMerge PRIVATE MacServices.mm)
  target_link_libraries(LibreMerge PRIVATE "-framework AppKit")
  # localized titles for the Finder Services menu entry
  target_sources(LibreMerge PRIVATE
    ${CMAKE_SOURCE_DIR}/packaging/en.lproj/ServicesMenu.strings
    ${CMAKE_SOURCE_DIR}/packaging/ca.lproj/ServicesMenu.strings
    ${CMAKE_SOURCE_DIR}/packaging/de.lproj/ServicesMenu.strings
    ${CMAKE_SOURCE_DIR}/packaging/es.lproj/ServicesMenu.strings
    ${CMAKE_SOURCE_DIR}/packaging/pt.lproj/ServicesMenu.strings)
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/en.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/en.lproj")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/ca.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/ca.lproj")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/de.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/de.lproj")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/es.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/es.lproj")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/packaging/pt.lproj/ServicesMenu.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/pt.lproj")
endif()

# WinMerge's preset file filters and the template of a new one, shipped as
# files (the engine reads them from a folder): the bundle's
# Resources/Filters on macOS, share/libremerge/Filters elsewhere, and next
# to a build's binary so that it runs uninstalled
file(GLOB LM_FILTER_FILES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/Filters/*.flt
  ${CMAKE_SOURCE_DIR}/Filters/FileFilter.tmpl)
if(APPLE)
  target_sources(LibreMerge PRIVATE ${LM_FILTER_FILES})
  set_source_files_properties(${LM_FILTER_FILES}
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Filters")
else()
  add_custom_command(TARGET LibreMerge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:LibreMerge>/Filters
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LM_FILTER_FILES}
      $<TARGET_FILE_DIR:LibreMerge>/Filters)
endif()

set_target_properties(LibreMerge PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER org.libremerge.LibreMerge
  MACOSX_BUNDLE_BUNDLE_VERSION 0.9.8
  MACOSX_BUNDLE_SHORT_VERSION_STRING 0.9.8
  MACOSX_BUNDLE_ICON_FILE AppIcon.icns
  MACOSX_BUNDLE_BUNDLE_NAME LibreMerge
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in
)

target_link_libraries(LibreMerge PRIVATE
  Qt6::Widgets
  Qt6::Concurrent
  LibArchive::LibArchive
  libremerge-engine
  libremerge-imgmerge
)

if(NOT WIN32)
  target_compile_options(LibreMerge PRIVATE
    -include ${CMAKE_SOURCE_DIR}/engine/ports/posix_compat.h)
endif()

if(UNIX AND NOT APPLE)
  # the desktop's light/dark setting, read from the XDG desktop portal
  find_package(Qt6 REQUIRED COMPONENTS DBus)
  target_sources(LibreMerge PRIVATE PortalAppearance.cpp)
  target_link_libraries(LibreMerge PRIVATE Qt6::DBus)
  target_compile_definitions(LibreMerge PRIVATE LM_HAVE_PORTAL)

  include(GNUInstallDirs)
  set_target_properties(LibreMerge PROPERTIES OUTPUT_NAME libremerge)
  install(TARGETS LibreMerge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/libremerge.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/linux/io.github.iagodpassos.libremerge.appdata.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES ${LM_FILTER_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/libremerge/Filters)
  foreach(_size 16 32 64 128 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/icons/libremerge-${_size}.png
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
      RENAME libremerge.png)
  endforeach()
endif()
