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
    src/core/AppArguments.cpp
    src/core/ArchivePath.cpp
    src/core/CommandQuery.cpp
    src/core/Json.cpp
    src/core/PortablePath.cpp
    src/core/Settings.cpp
    src/core/TerminalCommand.cpp
)
target_include_directories(simplefiler_core PUBLIC src)

if(WIN32)
    target_compile_definitions(simplefiler_core PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX
    )
endif()

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

    add_library(simplefiler_win
        src/win/CommandController.cpp
        src/win/FileEnumerator.cpp
        src/win/FileOperationController.cpp
        src/win/PaneController.cpp
        src/win/SidebarController.cpp
        src/win/ShellMenuController.cpp
        src/win/ShellOperations.cpp
        src/win/TerminalController.cpp
        src/win/TerminalLauncher.cpp
        src/win/ZipController.cpp
        src/win/ZipOperations.cpp
    )
    target_include_directories(simplefiler_win PUBLIC src)
    target_compile_definitions(simplefiler_win PUBLIC
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000
    )
    if(MSVC)
        target_compile_options(simplefiler_win PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(simplefiler_win PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    target_link_libraries(simplefiler_win PUBLIC
        simplefiler_core
        MINIZIP::minizip
        comctl32 comdlg32 shell32 shlwapi ole32 oleaut32 uuid propsys
        advapi32 uxtheme user32 version
    )

    add_executable(SimpleFiler WIN32
        src/win/App.cpp
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
        target_link_options(SimpleFiler PRIVATE /MANIFEST:NO)
    else()
        target_compile_options(SimpleFiler PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_options(SimpleFiler PRIVATE -municode -static)
    endif()
    target_link_libraries(SimpleFiler PRIVATE
        simplefiler_win
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
    add_executable(simplefiler_app_arguments_tests
        tests/AppArgumentsTests.cpp
    )
    target_link_libraries(simplefiler_app_arguments_tests
        PRIVATE simplefiler_core
    )
    if(MSVC)
        target_compile_options(simplefiler_app_arguments_tests
            PRIVATE /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(simplefiler_app_arguments_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
        NAME simplefiler_app_arguments_tests
        COMMAND simplefiler_app_arguments_tests
    )
    add_executable(simplefiler_portable_path_tests
        tests/PortablePathTests.cpp
    )
    target_link_libraries(simplefiler_portable_path_tests
        PRIVATE simplefiler_core
    )
    if(MSVC)
        target_compile_options(simplefiler_portable_path_tests
            PRIVATE /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(simplefiler_portable_path_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
        NAME simplefiler_portable_path_tests
        COMMAND simplefiler_portable_path_tests
    )
    if(WIN32)
        add_executable(simplefiler_windows_file_tests
            tests/WindowsFileOperationsTests.cpp
        )
        target_include_directories(simplefiler_windows_file_tests PRIVATE src)
        target_compile_definitions(simplefiler_windows_file_tests PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
            _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000
        )
        if(MSVC)
            target_compile_options(simplefiler_windows_file_tests
                PRIVATE /W4 /permissive- /utf-8
            )
        endif()
        target_link_libraries(simplefiler_windows_file_tests PRIVATE
            simplefiler_win
        )
        add_test(
            NAME simplefiler_windows_file_tests
            COMMAND simplefiler_windows_file_tests
        )
        set_tests_properties(
            simplefiler_windows_file_tests PROPERTIES TIMEOUT 30
        )
        add_executable(simplefiler_file_enumerator_tests
            tests/FileEnumeratorTests.cpp
        )
        target_include_directories(simplefiler_file_enumerator_tests
            PRIVATE src
        )
        target_compile_definitions(simplefiler_file_enumerator_tests PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
            _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000
        )
        if(MSVC)
            target_compile_options(simplefiler_file_enumerator_tests
                PRIVATE /W4 /permissive- /utf-8
            )
        endif()
        target_link_libraries(simplefiler_file_enumerator_tests PRIVATE
            simplefiler_win
        )
        add_test(
            NAME simplefiler_file_enumerator_tests
            COMMAND simplefiler_file_enumerator_tests
        )
        set_tests_properties(
            simplefiler_file_enumerator_tests PROPERTIES TIMEOUT 120
        )
        add_executable(simplefiler_controller_tests
            tests/ControllerTests.cpp
        )
        target_link_libraries(simplefiler_controller_tests PRIVATE
            simplefiler_win
        )
        if(MSVC)
            target_compile_options(simplefiler_controller_tests
                PRIVATE /W4 /permissive- /utf-8
            )
        endif()
        add_test(
            NAME simplefiler_controller_tests
            COMMAND simplefiler_controller_tests
        )
        set_tests_properties(
            simplefiler_controller_tests PROPERTIES TIMEOUT 30
        )
        add_test(
            NAME simplefiler_ui_smoke
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/UiSmoke.ps1
                    -Executable $<TARGET_FILE:SimpleFiler>
        )
        set_tests_properties(simplefiler_ui_smoke PROPERTIES TIMEOUT 45)
    endif()
endif()

if(WIN32)
    install(TARGETS SimpleFiler RUNTIME DESTINATION .)
    install(FILES
        README.md
        PLAN.md
        LICENSE
        THIRD_PARTY_NOTICES.md
        DESTINATION .
    )
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME "SimpleFiler")
    set(CPACK_PACKAGE_VENDOR "SimpleFiler")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME
        "SimpleFiler-${PROJECT_VERSION}-windows-x64"
    )
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()
