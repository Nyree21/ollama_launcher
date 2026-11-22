# Build notes

Tested with Qt 6.8.3 (MinGW 13.1) and CMake on Windows 11. Qt 5.15 is kept
buildable on a best-effort basis; the project only uses Widgets and Network.

## Prerequisites

- Qt 6.2 or newer with the **Widgets** and **Network** modules. Install the kit
  you intend to use (MinGW or MSVC) from the Qt online installer.
- CMake 3.16 or newer.
- Ninja (bundled with Qt under `Tools/Ninja`) or the Visual Studio generator.
- An Ollama installation to monitor. The launcher itself does not need Qt at
  build time beyond the modules above.

## MinGW

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 \
      -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build
```

## MSVC

Open an *x64 Native Tools Command Prompt* and run:

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

With a multi-config generator the binary is written to
`build/bin/Release/ollama_launcher.exe`.

## Deploy the Qt runtime

`windeployqt` must come from the **same kit** that was used to build, otherwise
the executable loads incompatible Qt DLLs and fails at start-up with error 127
or a "procedure entry point not found" message.

```bash
C:/Qt/6.8.3/mingw_64/bin/windeployqt --release build/bin/ollama_launcher.exe
```

For a portable folder, copy the executable somewhere else first and run
`windeployqt` there; it adds the `platforms/qwindows.dll` plugin, the style
plugin and the Network TLS backends next to the executable.

## Run

```bash
build/bin/ollama_launcher.exe
```

The first start creates `ollama_launcher.ini` next to the executable. Use
`--config <path>` to keep the configuration elsewhere, and `--minimized` to
start hidden in the tray.

To see `qDebug()` output while debugging, configure with `-DOL_CONSOLE=ON` so a
console window is attached to the process.

## CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `OL_CONSOLE` | `OFF` | attach a console window for log output |

## Target layout

A single `ollama_launcher` executable compiles every source file listed in
`OL_SOURCES` and adds all `include/<module>` directories to its include path, so
a translation unit can include a header from any module by plain file name.

## Other platforms

The project compiles on Linux and macOS, but the platform module only provides
empty stubs there: memory usage, VRAM and the client list stay empty, while
service control, logs and the proxy keep working. Contributions for a Linux
implementation of the process accounting are welcome.
