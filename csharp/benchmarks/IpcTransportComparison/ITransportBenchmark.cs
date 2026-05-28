namespace XprocSharp.Benchmarks.IpcTransportComparison;

internal interface ITransportBenchmark
{
    string Name { get; }

    TransportRunResult Run(BenchmarkSpec spec);
}

internal readonly record struct TransportRunResult(TimeSpan Elapsed, ulong Checksum);
