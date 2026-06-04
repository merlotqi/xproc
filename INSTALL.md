# Install xproc

This file covers installation layout and downstream consumption. For building from
source, see [BUILD.md](BUILD.md).

## What Gets Installed

`cmake --install` installs:

- C++ public headers under `include/xproc/...`
- C API header as `include/xproc_c.h`
- `xproc` and `xproc_c` libraries under `lib/` (or platform equivalent)
- CMake package files under `lib/cmake/xproc/`
- `pkg-config` file at `lib/pkgconfig/xproc.pc`

## Install to a Prefix

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /usr/local
```

To stage into a temporary directory for packaging:

```bash
cmake --install build --prefix /tmp/xproc-stage
```

## Header Layout

After installation, consumers should expect includes like:

```cpp
#include <xproc/xproc.hpp>
#include <xproc_c.h>
```

## Use with CMake

```cmake
find_package(xproc CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE xproc::xproc)
```

For the C ABI wrapper:

```cmake
find_package(xproc CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE xproc::xproc_c)
```

## Use with pkg-config

Point `PKG_CONFIG_PATH` at the installed `pkgconfig` directory if needed:

```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
pkg-config --cflags --libs xproc
```

For static linking on Linux:

```bash
pkg-config --libs --static xproc
```

`Libs.private` includes the Linux thread/runtime flags needed for a static
`libxproc.a`.

## Notes

- On Linux, downstream CMake consumers automatically pick up `Threads`.
- `xproc_c.h` is installed at the include root by design so bindings can use a
  short include path.
