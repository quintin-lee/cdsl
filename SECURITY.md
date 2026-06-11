# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in C-DSL, please report it responsibly:

1. **Do not open a public issue** — security issues should be reported privately
2. Email the maintainer at: `2449164582@qq.com`
3. Include:
   - A description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Suggested fix (if any)

## Response Timeline

- **Acknowledgment**: Within 48 hours
- **Initial assessment**: Within 7 days
- **Fix release**: Within 30 days (critical issues prioritized)

## Security Best Practices

When using C-DSL in production:

- Enable sandboxing (timeouts, memory limits, instruction limits)
- Validate all DSL input before parsing
- Use schema verification to restrict variables and actions
- Run with AddressSanitizer during development
- Keep dependencies updated (Flex, Bison, libcurl)

## Known Security Features

- Per-execution timeout (`cdsl_vm_set_timeout`)
- Memory limit (`cdsl_vm_set_memory_limit`)
- Instruction limit (`cdsl_vm_set_instruction_limit`)
- Read-only variables (`cdsl_schema_register_var_rw`)
- AST depth limits (max 64)
- Arena allocator for bounded memory

## Disclosure Policy

We follow a coordinated disclosure policy. After a fix is released:

1. Credit the reporter (with permission)
2. Publish a security advisory on GitHub
3. Update CHANGELOG.md with security fix details
