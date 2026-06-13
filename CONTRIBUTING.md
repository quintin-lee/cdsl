# Contributing to C-DSL

Thank you for your interest in contributing to C-DSL! This document provides guidelines and instructions for contributing.

## Development Setup

### Requirements

| Tool     | Minimum Version |
|----------|-----------------|
| C23 compiler (GCC / Clang / MSVC) | —               |
| CMake    | 3.19            |
| Flex     | 2.6             |
| Bison    | 3.8             |

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Running Tests

```bash
ctest --test-dir build --output-on-failure
```

### Running Benchmarks

```bash
cmake -B build -DCDSL_BUILD_BENCHMARKS=ON
cmake --build build -j$(nproc)
./build/cdsl_bench
```

### Running clang-tidy

```bash
cmake --build build --target check-tidy
```

### Building with coverage

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCDSL_ENABLE_COVERAGE=ON
cmake --build build -j$(nproc)
ctest --test-dir build
# coverage report in build/coverage/index.html
```

## Code Style

- C23 standard
- Use `clang-format` with the provided `.clang-format` file
- **Install the pre-commit hook** to auto-format before every commit:
  ```bash
  cmake --build build --target install-git-hooks
  ```
  This copies `cmake/pre-commit.hook` into `.git/hooks/pre-commit` and ensures all staged `.c/.h` files are formatted before commit.
- Run `cmake --build build --target check-format` before committing (or let the hook do it)
- Run `cmake --build build --target check-tidy` to check clang-tidy compliance
- All public functions must be documented with Doxygen comments

## Commit Message Convention

Use the following format:

```
<type>(<scope>): <subject>
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, no logic change)
- `refactor`: Code refactoring
- `test`: Adding or fixing tests
- `build`: Build system changes
- `ci`: CI/CD changes
- `chore`: Other changes

Examples:
- `feat(vm): add execution timeout support`
- `fix(parser): handle empty string literals`
- `docs(api): add missing parameter docs`
- `ci(workflows): add code coverage job`

## Pull Request Process

1. Fork the repository and create a feature branch
2. Install the pre-commit hook: `cmake --build build --target install-git-hooks`
3. Ensure all tests pass: `ctest --test-dir build --output-on-failure`
4. Ensure code formatting is correct: `cmake --build build --target check-format`
5. Run clang-tidy: `cmake --build build --target check-tidy`
6. Update documentation if needed (Doxygen, README, docs/)
7. Submit the PR using the [template](.github/PULL_REQUEST_TEMPLATE.md)

## Reporting Issues

- Use GitHub Issues
- Include reproduction steps for bugs
- Include environment info (OS, compiler, CMake version)
- For security issues, see SECURITY.md

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
