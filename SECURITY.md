# Security Policy

## Supported Versions

Security fixes are provided on a best-effort basis for the latest code on `main`.
If tagged releases are present, the latest minor release in the latest major line is the primary supported target.

## Reporting a Vulnerability

Please do **not** open a public GitHub issue for security vulnerabilities.

Report privately via email:

- `merlotrain.mc@gmail.com`

Include as much detail as possible:

- Affected version / commit hash
- Platform (`Linux` or `Windows`) and toolchain details
- Reproduction steps or proof-of-concept
- Impact assessment (crash, data corruption, privilege boundary concerns, etc.)

## Response Targets

- Initial acknowledgement: within **3 business days**
- Triage / severity assessment: within **7 business days**
- Fix timeline: communicated after triage based on severity and exploitability

## Disclosure Process

1. Maintainer confirms receipt and reproduces issue.
2. A fix is prepared and validated in CI.
3. A coordinated disclosure date is agreed when possible.
4. Public advisory and changelog entry are published after a fix is available.

## Scope Notes

- This project is an SPSC IPC library and assumes trusted local processes by default.
- Reports involving undefined behavior, memory corruption, races, or malformed input handling are in scope.
