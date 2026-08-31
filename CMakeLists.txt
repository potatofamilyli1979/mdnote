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

# Qt::DBus backs KDBusService (single-instance + activation) on Linux --
# not requested on Windows, where main_win.cpp uses QLocalSocket/
# QLocalServer for the same purpose instead, and a default Windows Qt
# install may not even ship the DBus module.
if(WIN32)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network LinguistTools)
else()
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui DBus Network LinguistTools)
endif()

# KDE Frameworks (config storage, global shortcuts, X11/Wayland window
# management, D-Bus service registration) and LayerShellQt (Wayland
# panel-style pinning) are Linux-only -- Windows uses its own
# implementations instead (ConfigManager_win.cpp, HotkeyManager_win.cpp,
# SlideWindow.cpp's Q_OS_WIN branch), with no equivalent dependency.
if(NOT WIN32)
  find_package(KF6Config REQUIRED)
  find_package(KF6WindowSystem REQUIRED)
  find_package(KF6GlobalAccel REQUIRED)
  find_package(KF6CoreAddons REQUIRED)
  find_package(KF6DBusAddons REQUIRED)
  find_package(LayerShellQt QUIET)
endif()

# main.cpp/ConfigManager.cpp/HotkeyManager.cpp are entirely different,
# non-overlapping implementations per platform (KDE Frameworks + D-Bus
# vs. plain Qt + Win32 APIs) -- kept as separate files rather than
# #ifdef-ing one shared file, since there's no meaningfully shared logic
# between them to preserve. SlideWindow.cpp and DialogUtils.cpp, by
# contrast, are mostly-shared logic with a handful of platform-specific
# fork points, so those stay single files with internal Q_OS_WIN guards.
if(WIN32)
  set(MDNOTE_PLATFORM_SOURCES
    src/main_win.cpp
    src/ConfigManager_win.cpp
    src/HotkeyManager_win.cpp
    # CMake compiles a .rc file listed as a target source like any other
    # -- this is what gives the built .exe its own icon (Explorer,
    # taskbar, Alt-Tab); without it a Windows binary has no icon at all.
    data/windows/mdnote.rc
  )
else()
  set(MDNOTE_PLATFORM_SOURCES
    src/main.cpp
    src/ConfigManager.cpp
    src/HotkeyManager.cpp
  )
endif()

add_executable(mdnote
  ${MDNOTE_PLATFORM_SOURCES}
  # ConfigManager.h/HotkeyManager.h have no same-basename .cpp among the
  # Windows sources (only ConfigManager_win.cpp/HotkeyManager_win.cpp),
  # which is enough to make AUTOMOC's header/moc pairing unreliable on
  # some generator+compiler combinations -- listing them explicitly
  # here removes any ambiguity about whether they're seen at all.
  # Harmless on Linux, where the matching .cpp already makes this
  # unambiguous.
  src/ConfigManager.h
  src/HotkeyManager.h
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

if(WIN32)
  # No console window behind the GUI.
  set_target_properties(mdnote PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(mdnote PRIVATE
  Qt6::Core
  Qt6::Widgets
  Qt6::Gui
  Qt6::Network
)

if(WIN32)
  # RegisterHotKey/UnregisterHotKey (HotkeyManager_win.cpp) and
  # SetWindowPos/SetForegroundWindow (SlideWindow.cpp's Q_OS_WIN branch)
  # live in user32, a standard system library present with any Windows
  # toolchain -- nothing extra to install for it.
  target_link_libraries(mdnote PRIVATE user32)
else()
  target_link_libraries(mdnote PRIVATE
    Qt6::DBus
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::WindowSystem
    KF6::GlobalAccel
    KF6::CoreAddons
    KF6::DBusAddons
  )
endif()

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

if(NOT WIN32)
  if(LayerShellQt_FOUND)
    message(STATUS "LayerShellQt found: enabling native Wayland panel-style pinning")
    target_link_libraries(mdnote PRIVATE LayerShellQt::Interface)
    target_compile_definitions(mdnote PRIVATE HAVE_LAYERSHELLQT=1)
  else()
    message(WARNING "LayerShellQt not found: Wayland sessions will fall back to plain top-level window behavior (no edge pinning/always-on-top)")
  endif()
endif()

install(TARGETS mdnote DESTINATION bin)
if(NOT WIN32)
  install(FILES data/org.kde.mdnote.desktop DESTINATION share/applications)
  install(FILES data/icons/hicolor/scalable/apps/org.kde.mdnote.svg
    DESTINATION share/icons/hicolor/scalable/apps)
endif()
