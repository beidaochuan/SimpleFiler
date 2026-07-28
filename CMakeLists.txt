cmake_minimum_required(VERSION 3.24)
project(SimpleFiler VERSION 0.1.0 LANGUAGES CXX)

option(SIMPLEFILER_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(simplefiler_core
    src/core/ArchivePath.cpp
    src/core/Json.cpp
    src/core/Settings.cpp
    src/core/TerminalCommand.cpp
)
target_include_directories(simplefiler_core PUBLIC src)

if(MSVC)
    target_compile_options(simplefiler_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(simplefiler_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    include(FetchContent)
    set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(MZ_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MZ_COMPAT OFF CACHE BOOL "" FORCE)
    set(MZ_FETCH_LIBS ON CACHE BOOL "" FORCE)
    set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
    set(MZ_LZMA OFF CACHE BOOL "" FORCE)
    set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
    set(MZ_OPENSSL OFF CACHE BOOL "" FORCE)
    set(MZ_LIBCOMP OFF CACHE BOOL "" FORCE)
    set(MZ_PKCRYPT OFF CACHE BOOL "" FORCE)
    set(MZ_WZAES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    set(ZLIB_TAG 2.3.3 CACHE STRING "" FORCE)
    set(WITH_GZFILEOP OFF CACHE BOOL "" FORCE)
    set(WITH_SSSE3 OFF CACHE BOOL "" FORCE)
    set(WITH_SSE41 OFF CACHE BOOL "" FORCE)
    set(WITH_SSE42 OFF CACHE BOOL "" FORCE)
    set(WITH_PCLMULQDQ OFF CACHE BOOL "" FORCE)
    set(WITH_AVX2 OFF CACHE BOOL "" FORCE)
    set(WITH_AVX512 OFF CACHE BOOL "" FORCE)
    set(WITH_AVX512VNNI OFF CACHE BOOL "" FORCE)
    set(WITH_VPCLMULQDQ OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        minizip
        URL https://github.com/zlib-ng/minizip-ng/archive/refs/tags/4.0.10.tar.gz
        URL_HASH SHA256=c362e35ee973fa7be58cc5e38a4a6c23cc8f7e652555daf4f115a9eb2d3a6be7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(minizip)

    add_executable(SimpleFiler WIN32
        src/win/App.cpp
        src/win/FileEnumerator.cpp
        src/win/ShellOperations.cpp
        src/win/TerminalLauncher.cpp
        src/win/ZipOperations.cpp
        src/win/main.cpp
        resources/SimpleFiler.rc
    )
    target_include_directories(SimpleFiler PRIVATE src)
    target_compile_definitions(SimpleFiler PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000
    )
    if(MSVC)
        target_compile_options(SimpleFiler PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(SimpleFiler PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_options(SimpleFiler PRIVATE -municode -static)
    endif()
    target_link_libraries(SimpleFiler PRIVATE
        simplefiler_core
        MINIZIP::minizip
        comctl32 shell32 shlwapi ole32 oleaut32 uuid propsys advapi32
    )
endif()

if(SIMPLEFILER_BUILD_TESTS)
    enable_testing()
    add_executable(simplefiler_tests tests/CoreTests.cpp)
    target_link_libraries(simplefiler_tests PRIVATE simplefiler_core)
    if(MSVC)
        target_compile_options(simplefiler_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(simplefiler_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME simplefiler_core_tests COMMAND simplefiler_tests)
endif()
