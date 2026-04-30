using System.Diagnostics;
using XprocSharp.Benchmarks.IpcTransportComparison;

TryElevatePriority();

var defaultSpecs = new[]
{
    new BenchmarkSpec(64, 1_000),
    new BenchmarkSpec(1024, 500),
    new BenchmarkSpec(4096, 250),
};

var allTransports = new ITransportBenchmark[]
{
    new XprocTransportPollCopyBenchmark(),
    new XprocTransportSpanBenchmark(),
    new NamedPipeTransportBenchmark(),
    new AnonymousPipeTransportBenchmark(),
    new MemoryMappedFileTransportBenchmark(),
};

var options = ParseOptions(args);
var specs = BuildSpecs(defaultSpecs, options);
var transports = allTransports
    .Where(transport => options.TransportNames.Count == 0 ||
                        options.TransportNames.Contains(transport.Name, StringComparer.OrdinalIgnoreCase))
    .ToArray();

if (transports.Length == 0)
{
    throw new InvalidOperationException("no matching transports selected");
}

Console.WriteLine("xproc C# transport comparison");
Console.WriteLine($"Host process id: {Environment.ProcessId}");
Console.WriteLine("Methodology: one producer thread hands off one fixed-size message at a time to one consumer thread.");
Console.WriteLine("Metric: end-to-end handoff time; when multiple measured runs are selected, the median is reported.");
Console.WriteLine("Note: xproc_fixed_pollcopy uses the current XprocSharp fixed-channel API (SendFixedSized + PollCopy).");
Console.WriteLine("Note: xproc_fixed_span uses the new non-allocating TryPollCopy(Span<byte>, out int) API.");
Console.WriteLine("Note: the MMF case is a minimal one-slot spin-wait handoff, not a general-purpose transport.");
Console.WriteLine($"Selected transports: {string.Join(", ", transports.Select(t => t.Name))}");
Console.WriteLine($"Measured runs: {options.MeasuredRuns}");

var results = BenchmarkRunner.RunAll(transports, specs, measuredRuns: options.MeasuredRuns);
BenchmarkRunner.PrintResults(results);

static void TryElevatePriority()
{
    try
    {
        using var process = Process.GetCurrentProcess();
        process.PriorityClass = ProcessPriorityClass.High;
    }
    catch
    {
        // Best effort only.
    }
}

static BenchmarkSpec[] BuildSpecs(BenchmarkSpec[] defaultSpecs, BenchmarkOptions options)
{
    if (options.Payloads.Count == 0)
    {
        return defaultSpecs;
    }

    if (options.Iterations.Count > 1 && options.Iterations.Count != options.Payloads.Count)
    {
        throw new InvalidOperationException("when multiple iteration counts are provided, they must match payload count");
    }

    var specs = new BenchmarkSpec[options.Payloads.Count];
    for (var index = 0; index < options.Payloads.Count; index += 1)
    {
        var iterations = options.Iterations.Count switch
        {
            0 => DefaultIterationsForPayload(options.Payloads[index]),
            1 => options.Iterations[0],
            _ => options.Iterations[index],
        };

        specs[index] = new BenchmarkSpec(options.Payloads[index], iterations);
    }

    return specs;
}

static int DefaultIterationsForPayload(int payloadSize)
{
    return payloadSize switch
    {
        <= 64 => 20_000,
        <= 1024 => 10_000,
        _ => 5_000,
    };
}

static BenchmarkOptions ParseOptions(string[] args)
{
    var options = new BenchmarkOptions();

    for (var index = 0; index < args.Length; index += 1)
    {
        switch (args[index])
        {
            case "--transport":
                index += 1;
                foreach (var name in args[index].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                {
                    options.TransportNames.Add(name);
                }
                break;
            case "--payloads":
                index += 1;
                foreach (var value in ParseIntList(args[index]))
                {
                    options.Payloads.Add(value);
                }
                break;
            case "--iterations":
                index += 1;
                foreach (var value in ParseIntList(args[index]))
                {
                    options.Iterations.Add(value);
                }
                break;
            case "--runs":
                index += 1;
                options.MeasuredRuns = int.Parse(args[index]);
                break;
            default:
                throw new InvalidOperationException($"unknown argument: {args[index]}");
        }
    }

    return options;
}

static IEnumerable<int> ParseIntList(string input)
{
    foreach (var token in input.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
    {
        yield return int.Parse(token);
    }
}

sealed class BenchmarkOptions
{
    public HashSet<string> TransportNames { get; } = [];

    public List<int> Payloads { get; } = [];

    public List<int> Iterations { get; } = [];

    public int MeasuredRuns { get; set; } = 3;
}
