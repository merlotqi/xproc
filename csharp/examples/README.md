# XprocSharp Examples

This folder contains runnable C# examples that mirror the core C++ demos where
the current `XprocSharp` surface is already a good fit.

## Build

From the repository root:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_CAPI_SHARED=ON
cmake --build build --target xproc_c --config Debug
dotnet build csharp/XprocSharp.sln
```

## Run

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

## What each example shows

- `FixedChannelInProcess`
  Minimal fixed-size send/receive flow over a shared-memory channel.

- `VarlenChannelInProcess`
  In-process variable-length messaging with ordered payload validation.

- `ObserverPeekDemo`
  Read-only observer peeking at traffic before a consumer drains it, plus a
  snapshot printout.

- `PingPong`
  Cross-process validation demo: the parent creates a fixed channel, launches a
  child copy of itself, streams `0..99`, and the child verifies the sequence.
