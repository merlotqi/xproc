# XprocSharp

`XprocSharp` is a first-cut C# binding for `xproc`, implemented over the stable
C API in [`capi/xproc_c.h`](../capi/xproc_c.h).

The current surface focuses on the core transport objects:

- `Producer`
- `Consumer`
- `Observer`
- `XprocShm` helpers for fixed / varlen shared-memory channels
- `Xproc` helpers for validation, size calculations, version, and cleanup

## Build the native library

From the repository root, build the C API as a shared library:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_CAPI_SHARED=ON
cmake --build build --target xproc_c --config Debug
```

`XprocSharp` resolves the native library by:

- first checking `XPROC_CSHARP_NATIVE_DIR`
- then checking the app base directory
- then checking common repo-local `build/capi/<Config>` paths

When in doubt, set the environment variable explicitly.

## Build the managed project

```bash
dotnet build csharp/XprocSharp/XprocSharp.csproj
```

Or build the full managed solution, including the example app:

```bash
dotnet build csharp/XprocSharp.sln
```

## Run the examples

Windows PowerShell:

```powershell
$env:XPROC_CSHARP_NATIVE_DIR = "$PWD\\build\\capi\\Debug"
dotnet run --project csharp/examples/FixedChannelInProcess/FixedChannelInProcess.csproj
dotnet run --project csharp/examples/VarlenChannelInProcess/VarlenChannelInProcess.csproj
dotnet run --project csharp/examples/ObserverPeekDemo/ObserverPeekDemo.csproj
dotnet run --project csharp/examples/PingPong/PingPong.csproj
```

Linux/macOS:

```bash
export XPROC_CSHARP_NATIVE_DIR="$PWD/build/capi"
dotnet run --project csharp/examples/FixedChannelInProcess/FixedChannelInProcess.csproj
dotnet run --project csharp/examples/VarlenChannelInProcess/VarlenChannelInProcess.csproj
dotnet run --project csharp/examples/ObserverPeekDemo/ObserverPeekDemo.csproj
dotnet run --project csharp/examples/PingPong/PingPong.csproj
```

See [csharp/examples/README.md](examples/README.md) for a short description of
each example.

## Run the benchmarks

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_CAPI_SHARED=ON
cmake --build build --target xproc_c --config Release
dotnet run --project csharp/benchmarks/IpcTransportComparison/IpcTransportComparison.csproj -c Release
```

See [csharp/benchmarks/README.md](benchmarks/README.md) for benchmark scope and
methodology notes.

## Notes

- `PollCopy()` / `PeekCopy()` return `byte[]?`, with `null` meaning "no message available".
- `TryPollCopy(Span<byte>, out int)` provides a non-allocating receive path for fixed-size workflows.
- Exceptions are surfaced as `XprocException`, including native `Status` and `LayoutError`.
- This first version is intentionally close to the C API; higher-level socket and packaging work can build on top of it.
