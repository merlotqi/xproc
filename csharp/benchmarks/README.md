# XprocSharp Benchmarks

This folder contains managed benchmarks that compare the current `XprocSharp`
surface with three built-in .NET IPC primitives:

- `xproc_fixed_pollcopy`
- `xproc_fixed_span`
- `named_pipe`
- `anonymous_pipe`
- `mmf_slot` (`MemoryMappedFile` with a minimal one-slot spin-wait handoff)

## Scope

These benchmarks focus on fixed-size lockstep handoff inside one process with
one producer thread and one consumer thread. Each iteration sends exactly one
message, waits until the consumer has received it, and only then starts the
next iteration. The default payload sizes match the current native
cross-framework benchmark set:

- `64 B`
- `1024 B`
- `4096 B`

The `xproc_fixed_pollcopy` case measures the current managed API shape:

- `Producer.SendFixedSized(ReadOnlySpan<byte>)`
- `Consumer.PollCopy()`

The `xproc_fixed_span` case uses the same producer path, but switches the
receive side to:

- `Consumer.TryPollCopy(Span<byte>, out int)`

That lets us separate the core transport cost from the extra allocation and
double-copy behavior in `PollCopy()`. The `mmf_slot` case is intentionally
lower-level: it is a raw `MemoryMappedFile` slot protocol, not a full-featured
transport equivalent to `xproc`.

## Build

From the repository root:

```bash
cmake -S . -B build -DXPROC_BUILD_CAPI=ON -DXPROC_BUILD_CAPI_SHARED=ON
cmake --build build --target xproc_c --config Release
dotnet build csharp/benchmarks/IpcTransportComparison/IpcTransportComparison.csproj -c Release
```

## Run

From the repository root:

```bash
dotnet run --project csharp/benchmarks/IpcTransportComparison/IpcTransportComparison.csproj -c Release
```

Useful overrides:

```bash
dotnet run --project csharp/benchmarks/IpcTransportComparison/IpcTransportComparison.csproj -c Release -- --runs 1
dotnet run --project csharp/benchmarks/IpcTransportComparison/IpcTransportComparison.csproj -c Release -- --transport xproc_fixed --payloads 64 --iterations 1000 --runs 1
dotnet run --project csharp/benchmarks/IpcTransportComparison/IpcTransportComparison.csproj -c Release -- --payloads 64,1024,4096 --iterations 1000,500,250 --runs 1
```

If native library discovery ever needs help, set:

- Windows PowerShell: `$env:XPROC_CSHARP_NATIVE_DIR = "$PWD\\build\\capi\\Release"`
- Linux shell: `export XPROC_CSHARP_NATIVE_DIR="$PWD/build/capi/Release"`
