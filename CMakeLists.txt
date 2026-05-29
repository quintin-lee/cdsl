cmake_minimum_required(VERSION 3.14)
project(cdsl VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Flex & Bison
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

bison_target(MyParser parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.c)
flex_target(MyLexer parser/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
add_flex_bison_dependency(MyLexer MyParser)

set(CDSL_SOURCES
    src/ast.c
    src/abstract.c
    src/execution.c
    src/cdsl_json.c
    src/cdsl_error.c
    src/cdsl_arena.c
    src/cdsl_hashmap.c
    src/ai_bridge.c
    ${BISON_MyParser_OUTPUTS}
    ${FLEX_MyLexer_OUTPUTS}
)

# 2. Static library
add_library(cdsl_static STATIC ${CDSL_SOURCES})
target_include_directories(cdsl_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
set_target_properties(cdsl_static PROPERTIES OUTPUT_NAME cdsl)
find_package(Threads REQUIRED)
target_link_libraries(cdsl_static PUBLIC Threads::Threads)

# 3. Shared library
add_library(cdsl_shared SHARED ${CDSL_SOURCES})
target_include_directories(cdsl_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_link_libraries(cdsl_shared PUBLIC Threads::Threads)
set_target_properties(cdsl_shared PROPERTIES
    OUTPUT_NAME cdsl
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 4. Default target alias
add_library(cdsl ALIAS cdsl_static)

# 5. Demo
add_executable(cdsl_demo demo/main.c)
target_link_libraries(cdsl_demo PRIVATE cdsl_static)

# 6. Tests
enable_testing()
add_executable(test_ast tests/test_ast.c)
target_link_libraries(test_ast PRIVATE cdsl_static)
add_test(NAME test_ast COMMAND test_ast)

add_executable(test_execution tests/test_execution.c)
target_link_libraries(test_execution PRIVATE cdsl_static)
add_test(NAME test_execution COMMAND test_execution)

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

# 10. Doxygen documentation
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
else()
    message(STATUS "Doxygen not found - 'doc' target will not be available")
endif()
