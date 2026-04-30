namespace XprocSharp.Benchmarks.IpcTransportComparison;

internal sealed record BenchmarkSpec(int PayloadSize, int Iterations);

internal sealed record BenchmarkResult(
    string Transport,
    int PayloadSize,
    int Iterations,
    double MedianNanosecondsPerMessage,
    double MedianMiBPerSecond,
    ulong Checksum);
