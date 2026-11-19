# Contributing

- One change per pull request, with tests.
- `./dev test` and `./dev lint` pass before a pull request is opened; CI runs
  the same checks.
- Commits follow Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`,
  `test:`, `ci:`, `refactor:`), one line.
- Changes that touch QNX code carry the output of the QNX build and target run
  in the pull request description, since CI does not have the QNX SDP.
