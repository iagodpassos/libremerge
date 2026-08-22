# Packaging helpers (macOS): app icon generator, built on demand.
if(APPLE)
  qt_add_executable(libremerge-icon-tool EXCLUDE_FROM_ALL
    icon_tool/main.cpp)
  target_link_libraries(libremerge-icon-tool PRIVATE Qt6::Gui)
endif()
