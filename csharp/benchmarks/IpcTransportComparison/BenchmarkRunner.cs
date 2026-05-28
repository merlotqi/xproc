using System.Diagnostics;

namespace XprocSharp.Benchmarks.IpcTransportComparison;

internal static class BenchmarkRunner
{
    public static IReadOnlyList<BenchmarkResult> RunAll(
        IReadOnlyList<ITransportBenchmark> transports,
        IReadOnlyList<BenchmarkSpec> specs,
        int measuredRuns)
    {
        var results = new List<BenchmarkResult>(transports.Count * specs.Count);
        foreach (var spec in specs)
        {
            foreach (var transport in transports)
            {
                Console.WriteLine();
                Console.WriteLine($"[{transport.Name}] payload={spec.PayloadSize}B iterations={spec.Iterations}");
                Console.WriteLine("  warmup...");
                WarmUp(transport, spec);

                var runs = new List<TransportRunResult>(measuredRuns);
                for (var run = 0; run < measuredRuns; run += 1)
                {
                    Console.WriteLine($"  run {run + 1}/{measuredRuns}...");
                    var result = transport.Run(spec);
                    runs.Add(result);
                    Console.WriteLine($"    elapsed={result.Elapsed.TotalMilliseconds:F2} ms checksum=0x{result.Checksum:X}");
                }

                runs.Sort((left, right) => left.Elapsed.CompareTo(right.Elapsed));
                var median = runs[runs.Count / 2];
                var seconds = median.Elapsed.TotalSeconds;
                var bytesProcessed = (double)spec.PayloadSize * spec.Iterations;
                results.Add(new BenchmarkResult(
                    transport.Name,
                    spec.PayloadSize,
                    spec.Iterations,
                    median.Elapsed.TotalSeconds * 1_000_000_000.0 / spec.Iterations,
                    bytesProcessed / seconds / (1024.0 * 1024.0),
                    median.Checksum));
            }
        }

        return results;
    }

    public static void PrintResults(IReadOnlyList<BenchmarkResult> results)
    {
        Console.WriteLine();
        Console.WriteLine("Transport                 Payload     Iterations      ns/msg        MiB/s");
        Console.WriteLine("--------------------------------------------------------------------------");

        foreach (var result in results)
        {
            Console.WriteLine(
                $"{result.Transport,-24} {result.PayloadSize,7} B {result.Iterations,13} {result.MedianNanosecondsPerMessage,11:F1} {result.MedianMiBPerSecond,12:F1}");
        }
    }

    private static void WarmUp(ITransportBenchmark transport, BenchmarkSpec spec)
    {
        var warmUpIterations = Math.Max(500, spec.Iterations / 10);
        transport.Run(spec with { Iterations = warmUpIterations });
    }
}
