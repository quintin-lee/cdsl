## Description

<!-- Briefly describe the change and why it's needed -->

## Type of Change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactoring / Code quality
- [ ] Documentation
- [ ] Build / CI
- [ ] Other (please describe):

## Checklist

- [ ] All 22+ C tests pass (`ctest --test-dir build --output-on-failure`)
- [ ] Code formatted (`cmake --build build --target check-format`)
- [ ] clang-tidy clean (`find src/ -name '*.c' | xargs clang-tidy -p build --quiet`)
- [ ] No new compiler warnings (`cmake --build build -j$(nproc) 2>&1 | grep -E "warning:|error:"`)
- [ ] Documentation updated (if applicable)
- [ ] Changes are backward-compatible (or migration path documented)

## Related Issues

<!-- Link any related issues or discussions -->
