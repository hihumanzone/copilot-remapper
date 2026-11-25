cmake_minimum_required(VERSION 3.15)
project(CopilotRemapper LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
target_compile_definitions(CopilotRemapper PRIVATE
    UNICODE
    _UNICODE
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

target_link_libraries(CopilotRemapper PRIVATE
    user32
    gdi32
    shell32
    comctl32
    dwmapi
    uxtheme
    advapi32
    ole32
    oleaut32
    uuid
    taskschd
)

if(MSVC)
    target_compile_options(CopilotRemapper PRIVATE /W4 /O2 /GL /Gy /Gw /GR- /EHsc)
    target_link_options(CopilotRemapper PRIVATE /LTCG /OPT:REF /OPT:ICF)
else()
    target_compile_options(CopilotRemapper PRIVATE
        -Wall -Wextra -Os -fno-exceptions -fno-rtti -fno-ident
        -ffunction-sections -fdata-sections
    )
    target_link_options(CopilotRemapper PRIVATE -mwindows -municode -static -s -Wl,--gc-sections)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set_property(TARGET CopilotRemapper PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()
