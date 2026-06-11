cmake_minimum_required(VERSION 3.14)
project(cdsl VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# 13. Code coverage (Moved up to apply to all targets)
option(CDSL_ENABLE_COVERAGE "Enable gcov code coverage reporting" OFF)
if(CDSL_ENABLE_COVERAGE)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Coverage works best with Debug builds. Current: ${CMAKE_BUILD_TYPE}")
    endif()
    add_compile_options(--coverage -fprofile-update=atomic)
    add_link_options(--coverage)
    message(STATUS "Code coverage enabled (--coverage -fprofile-update=atomic)")
endif()

# MSVC security warnings
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# 1. Flex & Bison
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# libcurl for AI bridge
find_package(CURL)
if(CURL_FOUND)
    message(STATUS "Found libcurl - native AI bridge networking enabled")
else()
    message(WARNING "libcurl not found - AI bridge will fallback to popen(\"curl ...\")")
endif()

# LibreOffice SDK for document parsing
set(CDSL_LO_INCLUDE_DIR "/usr/include/libreoffice")
set(CDSL_LO_LIB_DIR "/usr/lib/libreoffice/program")
if(EXISTS "${CDSL_LO_INCLUDE_DIR}/LibreOfficeKit/LibreOfficeKit.h" AND
   EXISTS "${CDSL_LO_LIB_DIR}/libuno_sal.so.3")
    message(STATUS "Found LibreOffice SDK - document parsing enabled (CDSL_USE_LIBREOFFICE)")
    list(APPEND CDSL_LO_SOURCES "src/doc/doc_parse.cpp" "src/doc/xml_parser.cpp")
    set(CDSL_LO_INCLUDE_DIRS "${CDSL_LO_INCLUDE_DIR}")
    set(CDSL_USE_LIBREOFFICE TRUE)
else()
    message(STATUS "LibreOffice SDK not found at ${CDSL_LO_INCLUDE_DIR} - document parsing disabled")
    set(CDSL_USE_LIBREOFFICE FALSE)
endif()

bison_target(MyParser parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.c)
flex_target(MyLexer parser/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
add_flex_bison_dependency(MyLexer MyParser)

file(GLOB_RECURSE CDSL_SOURCES CONFIGURE_DEPENDS "src/*.c")
list(APPEND CDSL_SOURCES
    ${BISON_MyParser_OUTPUTS}
    ${FLEX_MyLexer_OUTPUTS}
)
if(CDSL_USE_LIBREOFFICE)
    list(APPEND CDSL_SOURCES ${CDSL_LO_SOURCES})
endif()

# 2. Object library (avoids compiling sources twice for static+shared)
find_package(Threads REQUIRED)
add_library(cdsl_objects OBJECT ${CDSL_SOURCES})
set_property(TARGET cdsl_objects PROPERTY POSITION_INDEPENDENT_CODE 1)
target_include_directories(cdsl_objects PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
if(CURL_FOUND)
    target_link_libraries(cdsl_objects PRIVATE CURL::libcurl)
    target_compile_definitions(cdsl_objects PRIVATE CDSL_USE_CURL)
endif()
if(CDSL_USE_LIBREOFFICE)
    set_source_files_properties(src/doc/doc_parse.cpp PROPERTIES LANGUAGE CXX)
    target_include_directories(cdsl_objects PRIVATE ${CDSL_LO_INCLUDE_DIRS})
    target_compile_definitions(cdsl_objects PRIVATE CDSL_USE_LIBREOFFICE)
    target_compile_definitions(cdsl_objects PRIVATE LOK_USE_UNSTABLE_API)
endif()

if(MSVC)
    target_compile_options(cdsl_objects PRIVATE /W4 /WX)
else()
    target_compile_options(cdsl_objects PRIVATE -Wall -Wextra -Wpedantic -Werror -Wshadow -Wformat=2)
endif()

# 3. Static library
add_library(cdsl_static STATIC $<TARGET_OBJECTS:cdsl_objects>)
target_include_directories(cdsl_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
set_target_properties(cdsl_static PROPERTIES OUTPUT_NAME cdsl)
target_link_libraries(cdsl_static PUBLIC Threads::Threads m)
if(CURL_FOUND)
    target_link_libraries(cdsl_static PRIVATE CURL::libcurl)
endif()

# 4. Shared library
add_library(cdsl_shared SHARED $<TARGET_OBJECTS:cdsl_objects>)
target_include_directories(cdsl_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_link_libraries(cdsl_shared PUBLIC Threads::Threads m)
if(CURL_FOUND)
    target_link_libraries(cdsl_shared PUBLIC CURL::libcurl)
endif()
set_target_properties(cdsl_shared PROPERTIES
    OUTPUT_NAME cdsl
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# 5. Default target alias
add_library(cdsl ALIAS cdsl_static)

# 6. Common warning flags for all executables
macro(cdsl_add_warning_flags target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wshadow -Wformat=2)
    endif()
endmacro()

# 7. Demo
add_executable(cdsl_demo demo/main.c)
cdsl_add_warning_flags(cdsl_demo)
target_link_libraries(cdsl_demo PRIVATE cdsl_static)

add_executable(cdsl_official_review demo/official_review.c)
cdsl_add_warning_flags(cdsl_official_review)
target_link_libraries(cdsl_official_review PRIVATE cdsl_static)

if(CDSL_USE_LIBREOFFICE)
    add_executable(cdsl_word_parser demo/word_parser.c)
    cdsl_add_warning_flags(cdsl_word_parser)
    target_link_libraries(cdsl_word_parser PRIVATE cdsl_static)
endif()

# 8. Tests
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.c")
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    
    if(${test_name} STREQUAL "test_doc" AND NOT CDSL_USE_LIBREOFFICE)
        continue()
    endif()

    add_executable(${test_name} ${test_src})
    cdsl_add_warning_flags(${test_name})
    target_link_libraries(${test_name} PRIVATE cdsl_static)
    add_test(NAME ${test_name} COMMAND ${test_name})
    if(${test_name} STREQUAL "test_doc")
        target_compile_definitions(${test_name} PRIVATE
            TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    endif()
endforeach()

# 8. Benchmarks
option(CDSL_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(CDSL_BUILD_BENCHMARKS)
    add_executable(cdsl_bench tests/bench_execution.c)
    target_link_libraries(cdsl_bench PRIVATE cdsl_static)
    message(STATUS "Benchmark target cdsl_bench enabled")
endif()

# 9. LSP server (optional)
option(CDSL_BUILD_LSP "Build the cdsl-lsp language server" OFF)
if(CDSL_BUILD_LSP)
    add_subdirectory(tools/cdsl-lsp)
endif()

install(TARGETS cdsl_static cdsl_shared
    EXPORT cdslTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/cdsl/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cdsl)
# parser.tab.h is internal; installed only for build-time access via
# BUILD_INTERFACE, not as a public API header

# 9. CMake package config
install(EXPORT cdslTargets
    FILE cdslTargets.cmake
    NAMESPACE cdsl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cdsl
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cdslConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdslConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cdslConfig.cmake"
    @ONLY
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cdslConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cdslConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cdsl
)

# 10. pkg-config
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdsl.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cdsl.pc"
    @ONLY
)

# 11. Python setup.py version
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py"
    "${CMAKE_CURRENT_BINARY_DIR}/python/setup.py"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cdsl.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# 11. Code formatting (clang-format)
option(CDSL_FORMAT_ON_BUILD "Run clang-format before every build" OFF)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE CDSL_FORMAT_FILES CONFIGURE_DEPENDS
        "include/*.h"
        "include/*/*.h"
        "include/*/*/*.h"
        "src/*.c"
        "src/*.cpp"
        "src/*.h"
        "src/*/*.c"
        "src/*/*.cpp"
        "src/*/*.h"
        "demo/*.c"
        "tests/*.c"
        "tests/*.h"
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i -style=file ${CDSL_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting all source files with clang-format"
    )

    add_custom_target(check-format
        COMMAND ${CLANG_FORMAT} --dry-run --Werror -style=file ${CDSL_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking code formatting (no changes made)"
    )

    add_custom_target(install-git-hooks
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pre-commit.hook
            ${CMAKE_CURRENT_SOURCE_DIR}/.git/hooks/pre-commit
        COMMAND chmod +x ${CMAKE_CURRENT_SOURCE_DIR}/.git/hooks/pre-commit
        COMMENT "Installing git pre-commit hook for auto-formatting"
    )

    find_program(CLANG_TIDY clang-tidy)
    if(CLANG_TIDY)
        add_custom_target(check-tidy
            COMMAND ${CMAKE_COMMAND} -E echo "Running clang-tidy on source files..."
            COMMAND find src/ -name '*.c' -not -name lexer.c -not -name parser.tab.c -exec ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --quiet {} +
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT ""
        )
    endif()

    if(CDSL_FORMAT_ON_BUILD)
        add_dependencies(cdsl_static format)
        add_dependencies(cdsl_shared format)
        add_dependencies(cdsl_demo format)
        message(STATUS "clang-format found - formatting ON on every build (CDSL_FORMAT_ON_BUILD=ON)")
    else()
        message(STATUS "clang-format found - targets: format, check-format, install-git-hooks")
        if(CLANG_TIDY)
            message(STATUS "clang-tidy found - target: check-tidy")
        endif()
        message(STATUS "  Enable build-time formatting: cmake -DCDSL_FORMAT_ON_BUILD=ON ..")
    endif()
else()
    message(STATUS "clang-format not found - format targets disabled")
endif()

# 12. Doxygen documentation
option(CDSL_GENERATE_DOCS "Generate Doxygen API documentation during build" OFF)

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    if(CDSL_GENERATE_DOCS)
        add_dependencies(cdsl_static doc)
        message(STATUS "Doxygen found - docs will be generated on every build (CDSL_GENERATE_DOCS=ON)")
        message(STATUS "  Disable: cmake -DCDSL_GENERATE_DOCS=OFF ..")
    else()
        message(STATUS "Doxygen found - run 'make doc' to generate docs (CDSL_GENERATE_DOCS=OFF)")
    endif()
else()
    message(STATUS "Doxygen not found - 'doc' target will not be available")
endif()

# 14. Fuzz target (requires Clang with libFuzzer)
option(CDSL_BUILD_FUZZ "Build fuzz targets (requires Clang)" OFF)
if(CDSL_BUILD_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CDSL_BUILD_FUZZ requires Clang (current: ${CMAKE_C_COMPILER_ID})")
    endif()
    add_executable(cdsl_fuzz_parse tests/fuzz_parse.c)
    target_link_libraries(cdsl_fuzz_parse PRIVATE cdsl_static)
    target_compile_options(cdsl_fuzz_parse PRIVATE -fsanitize=fuzzer,address)
    target_link_options(cdsl_fuzz_parse PRIVATE -fsanitize=fuzzer,address)
    message(STATUS "Fuzz target cdsl_fuzz_parse enabled (Clang + libFuzzer)")
endif()
