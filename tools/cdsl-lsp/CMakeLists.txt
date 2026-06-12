add_executable(cdsl-lsp main.c)
target_link_libraries(cdsl-lsp PRIVATE cdsl_static Threads::Threads m)
target_include_directories(cdsl-lsp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
install(TARGETS cdsl-lsp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
