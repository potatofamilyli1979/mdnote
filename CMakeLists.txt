cmake_minimum_required(VERSION 3.16)
project(mdnote VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui DBus Network LinguistTools)
find_package(KF6Config REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(LayerShellQt QUIET)

add_executable(mdnote
  src/main.cpp
  src/ConfigManager.cpp
  src/HotkeyManager.cpp
  src/SlideWindow.cpp
  src/Sidebar.cpp
  src/FileManager.cpp
  src/EditorArea.cpp
  src/MarkdownHighlighter.cpp
  src/CodeHighlighter.cpp
  src/NormalTextEdit.cpp
  src/MarkdownExporter.cpp
  src/DialogUtils.cpp
  src/Theme.cpp
  src/IconGlyphs.cpp
  src/SidebarListModel.cpp
  src/SidebarFilterProxyModel.cpp
  src/SidebarItemDelegate.cpp
  src/ThemePicker.cpp
  src/OverlayScrollBar.cpp
)

target_link_libraries(mdnote PRIVATE
  Qt6::Core
  Qt6::Widgets
  Qt6::Gui
  Qt6::DBus
  Qt6::Network
  KF6::ConfigCore
  KF6::ConfigGui
  KF6::WindowSystem
  KF6::GlobalAccel
  KF6::CoreAddons
  KF6::DBusAddons
)

# English is this app's tr() source language, so it needs no translation
# file of its own -- only the two actual translations below. Embedded
# into the binary as Qt resources (RESOURCE_PREFIX) rather than
# installed as loose files, so a Debian package (or any other install
# layout) doesn't need to know a separate translations path.
qt6_add_translations(mdnote
  TS_FILES
    translations/mdnote_zh_Hans.ts
    translations/mdnote_zh_Hant.ts
  RESOURCE_PREFIX "/i18n"
)

if(LayerShellQt_FOUND)
  message(STATUS "LayerShellQt found: enabling native Wayland panel-style pinning")
  target_link_libraries(mdnote PRIVATE LayerShellQt::Interface)
  target_compile_definitions(mdnote PRIVATE HAVE_LAYERSHELLQT=1)
else()
  message(WARNING "LayerShellQt not found: Wayland sessions will fall back to plain top-level window behavior (no edge pinning/always-on-top)")
endif()

install(TARGETS mdnote DESTINATION bin)
install(FILES data/org.kde.mdnote.desktop DESTINATION share/applications)
