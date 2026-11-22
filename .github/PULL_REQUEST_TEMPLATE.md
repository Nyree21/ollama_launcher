## What does this change?

<!-- A sentence or two. Link the issue it closes, e.g. "Closes #12". -->

## Why?

<!-- The problem being solved, not just the code being changed. -->

## How was it tested?

<!--
- OS and Qt version used to build
- Which screen or feature you exercised
- Anything you deliberately did not test
-->

## Checklist

- [ ] Builds without warnings (`cmake --build build`)
- [ ] No new user-visible string was added without a matching entry in `src/core/i18n.cpp`
- [ ] Comments and identifiers are in English; no leftover debug output
- [ ] Changes are focused on one topic (unrelated cleanups go in a separate PR)
- [ ] `README.md` / `README.zh-CN.md` updated if behaviour or configuration changed
- [ ] No build output, binary or local configuration file is included
