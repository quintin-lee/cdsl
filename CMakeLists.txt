cmake_minimum_required(VERSION 3.14)
project(cdsl VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Flex & Bison
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# libcurl for AI bridge
find_package(CURL)
if(CURL_FOUND)
    message(STATUS "Found libcurl - native AI bridge networking enabled")
    add_definitions(-DCDSL_USE_CURL)
else()
    message(WARNING "libcurl not found - AI bridge will fallback to popen(\"curl ...\")")
endif()

bison_target(MyParser parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.c)
flex_target(MyLexer parser/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
add_flex_bison_dependency(MyLexer MyParser)

file(GLOB CDSL_SOURCES CONFIGURE_DEPENDS "src/*.c")
list(APPEND CDSL_SOURCES
    ${BISON_MyParser_OUTPUTS}
    ${FLEX_MyLexer_OUTPUTS}
)

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
endif()

if(MSVC)
    target_compile_options(cdsl_objects PRIVATE /W4 /WX)
else()
    target_compile_options(cdsl_objects PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# 3. Static library
add_library(cdsl_static STATIC $<TARGET_OBJECTS:cdsl_objects>)
target_include_directories(cdsl_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
set_target_properties(cdsl_static PROPERTIES OUTPUT_NAME cdsl)
target_link_libraries(cdsl_static PUBLIC Threads::Threads)
if(CURL_FOUND)
    target_link_libraries(cdsl_static PRIVATE CURL::libcurl)
endif()

# 4. Shared library
add_library(cdsl_shared SHARED $<TARGET_OBJECTS:cdsl_objects>)
target_include_directories(cdsl_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_link_libraries(cdsl_shared PUBLIC Threads::Threads)
if(CURL_FOUND)
    target_link_libraries(cdsl_shared PRIVATE CURL::libcurl)
endif()

# 5. Default target alias
add_library(cdsl ALIAS cdsl_static)

# 5. Demo
add_executable(cdsl_demo demo/main.c)
target_link_libraries(cdsl_demo PRIVATE cdsl_static)

# 6. Tests
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.c")
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE cdsl_static)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# 7. Install targets
include(GNUInstallDirs)

install(TARGETS cdsl_static cdsl_shared
    EXPORT cdslTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cdsl)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cdsl)

# 8. CMake package config
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

# 9. pkg-config
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cdsl.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cdsl.pc"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cdsl.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# 10. Code formatting (clang-format)
option(CDSL_FORMAT_ON_BUILD "Run clang-format before every build" ON)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE CDSL_FORMAT_FILES CONFIGURE_DEPENDS
        "include/*.h"
        "src/*.c"
        "src/*.h"
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

    if(CDSL_FORMAT_ON_BUILD)
        add_dependencies(cdsl_static format)
        add_dependencies(cdsl_shared format)
        add_dependencies(cdsl_demo format)
        message(STATUS "clang-format found - formatting ON on every build (CDSL_FORMAT_ON_BUILD=ON)")
    else()
        message(STATUS "clang-format found - targets: format, check-format, install-git-hooks")
        message(STATUS "  Enable build-time formatting: cmake -DCDSL_FORMAT_ON_BUILD=ON ..")
    endif()
else()
    message(STATUS "clang-format not found - format targets disabled")
endif()

# 11. Doxygen documentation
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
