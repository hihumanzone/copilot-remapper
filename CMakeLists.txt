cmake_minimum_required(VERSION 3.15)
project(CopilotRemapper LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/ActionRunner.cpp
    src/KeyboardHook.cpp
    src/Theme.cpp
    src/IconHelper.cpp
    src/SettingsWindow.cpp
    src/TrayIcon.cpp
    src/SingleInstance.cpp
    src/ProcessManager.cpp
    resource.rc
)

add_executable(CopilotRemapper WIN32 ${SOURCES})

target_include_directories(CopilotRemapper PRIVATE src .)

target_link_libraries(CopilotRemapper PRIVATE
    user32
    gdi32
    gdiplus
    shell32
    comctl32
    dwmapi
    uxtheme
    advapi32
    ole32
    uuid
)

if(MSVC)
    target_compile_options(CopilotRemapper PRIVATE /W4 /O2 /EHsc)
else()
    target_compile_options(CopilotRemapper PRIVATE -Wall -Wextra -O3)
    target_link_options(CopilotRemapper PRIVATE -mwindows -static -s)
endif()
