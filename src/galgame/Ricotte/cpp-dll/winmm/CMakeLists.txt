cmake_minimum_required(VERSION 3.20)
set(PROJECT_NAME winmm)
project(${PROJECT_NAME} LANGUAGES C CXX)

# ---- UTF-8 源文件编码 ----
if(MSVC)
    add_compile_options("/utf-8")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
# 中间静态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/lib/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/lib/$<CONFIG>)


# ---- 目标平台强制 x86 ----
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "This project must be built as 32-bit (x86). Use -A Win32 with CMake.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 静态链接 CRT (/MT release, /MTd debug) ----
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- 查找 Detours (vcpkg) ----
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)

# ---- 输出 dll ----
add_library(${PROJECT_NAME} SHARED
    src/dllmain.cpp
    src/MyHook.cpp
)

# 头文件目录（只留正确的）
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${DETOURS_INCLUDE_DIRS}
)

# 链接库（只留正确的）
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${DETOURS_LIBRARY}
    user32
    shlwapi
)

# ---- 导出全部符号----
set_target_properties(${PROJECT_NAME} PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# ---- x86 平台特定 ----
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
