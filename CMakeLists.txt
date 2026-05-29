cmake_minimum_required(VERSION 3.14)
project(cdsl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. 查找 Flex 与 Bison
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# 2. 指定 Flex & Bison 编译规则
bison_target(MyParser parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.c)
flex_target(MyLexer parser/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.c)
add_flex_bison_dependency(MyLexer MyParser)

# 4. 编译 cdsl 核心静态库
add_library(cdsl STATIC
    src/ast.c
    src/abstract.c
    src/execution.c
    src/cdsl_json.c
    src/ai_bridge.c
    ${BISON_MyParser_OUTPUTS}
    ${FLEX_MyLexer_OUTPUTS}
)

# 5. 头文件包含路径
target_include_directories(cdsl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

# 5. CLI Demo 可执行程序
add_executable(cdsl_demo demo/main.c)
target_link_libraries(cdsl_demo PRIVATE cdsl)
