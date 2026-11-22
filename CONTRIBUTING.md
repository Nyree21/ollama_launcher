# Contributing

Thanks for taking the time to help. Bug reports, fixes and translations are all
welcome. By taking part you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Workflow

Only the maintainer can push to `main`, so contributions go through a fork and a
pull request:

1. Fork the repository and create a topic branch (`fix/proxy-port`,
   `feature/export-metrics`).
2. Make the change, build it locally and run it.
3. Open a pull request against `main`. The template asks for what changed and how
   it was tested — a short paragraph is enough.
4. Keep the discussion in the pull request. Squash or rebase on request; do not
   force-push a branch that is already under review unless you are asked to.

Small fixes (typos, one-line bug fixes) are fine as a pull request without an
issue. Anything that changes behaviour, configuration or the UI is better
discussed in an issue first, so nobody writes code that cannot be merged.

## Reporting a bug

Please include:

- Windows version and display scaling (100%, 125%, 150%).
- Qt version used to build, and whether the binary came from a release or from
  your own build.
- The Ollama version (`ollama --version`).
- Steps to reproduce, what you expected and what happened.
- The relevant part of the log tab — it can be copied with a right click or
  exported with **Export logs**.

## Pull requests

- Keep the module boundaries: `core` must not depend on `ui`, and only `ui`
  knows about widgets.
- Include headers by plain file name (`#include "app_config.h"`). Every
  `include/<module>` directory is on the compiler path, so no `../` escapes.
- Comments and identifiers are English. User-facing strings go through
  `T("English text")`, and the Chinese translation is added to
  `src/core/i18n.cpp` in the same change.
- Keep the code free of new dependencies. The launcher uses Qt and the Win32 API
  only.
- Do not add assets you did not create, or assets whose licence is incompatible
  with MIT.

Build and run before sending the change:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<your Qt kit>
cmake --build build
```

## Translations

The translation table lives in `src/core/i18n.cpp`: the English string is the
key, and the second column holds the translation. Adding a third language means
adding a column and one entry to the `Language` enum, plus the matching item in
the settings dialog.
