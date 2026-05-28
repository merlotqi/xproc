using System.Diagnostics;
using System.Reflection;
using XprocSharp;

const ulong SchemaId = 0x2026043004UL;
const int MessageCount = 100;

return args.Length > 0 && string.Equals(args[0], "--child", StringComparison.Ordinal)
    ? RunChild(args)
    : RunParent();

int RunParent()
{
    var path = $"/xproc_csharp_ping_pong_{Environment.ProcessId}_{Guid.NewGuid():N}";
    CleanupShm(path);

    Process? child = null;

    try
    {
        var created = XprocShm.CreateFixedChannel(
            path,
            itemSize: sizeof(uint),
            dataCapacity: 65536,
            dataAlign: 8,
            schemaId: SchemaId);

        using var producer = created.OpenProducer();
        child = StartChildProcess(path);

        for (uint value = 0; value < MessageCount; value += 1)
        {
            producer.SendFixedSized(BitConverter.GetBytes(value));
        }

        child.WaitForExit();
        if (child.ExitCode != 0)
        {
            Console.Error.WriteLine($"child failed with exit code {child.ExitCode}");
            return 1;
        }

        Console.WriteLine("ping_pong ok");
        return 0;
    }
    finally
    {
        child?.Dispose();
        CleanupShm(path);
    }
}

int RunChild(string[] childArgs)
{
    if (childArgs.Length < 2)
    {
        Console.Error.WriteLine("missing shared-memory path");
        return 2;
    }

    var path = childArgs[1];

    try
    {
        using var consumer = XprocShm.AttachFixedChannel(path, schemaId: SchemaId).OpenConsumer();
        for (uint expected = 0; expected < MessageCount; expected += 1)
        {
            byte[] payload;
            while (true)
            {
                payload = consumer.PollCopy()!;
                if (payload is not null)
                {
                    break;
                }

                consumer.WaitForData();
            }

            if (payload.Length != sizeof(uint))
            {
                Console.Error.WriteLine($"unexpected payload length: {payload.Length}");
                return 3;
            }

            var actual = BitConverter.ToUInt32(payload, 0);
            if (actual != expected)
            {
                Console.Error.WriteLine($"sequence mismatch, expected {expected} got {actual}");
                return 4;
            }
        }

        Console.WriteLine("child validated 100 messages");
        return 0;
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine(ex);
        return 5;
    }
}

Process StartChildProcess(string path)
{
    var entryAssemblyPath = Assembly.GetEntryAssembly()?.Location;
    if (string.IsNullOrWhiteSpace(entryAssemblyPath))
    {
        throw new InvalidOperationException("unable to resolve entry assembly path");
    }

    var startInfo = CreateChildStartInfo(entryAssemblyPath);
    startInfo.ArgumentList.Add("--child");
    startInfo.ArgumentList.Add(path);
    startInfo.UseShellExecute = false;

    return Process.Start(startInfo) ?? throw new InvalidOperationException("failed to start child process");
}

ProcessStartInfo CreateChildStartInfo(string entryAssemblyPath)
{
    var processPath = Environment.ProcessPath;
    if (!string.IsNullOrWhiteSpace(processPath) &&
        string.Equals(Path.GetFileNameWithoutExtension(processPath), "dotnet", StringComparison.OrdinalIgnoreCase))
    {
        var startInfo = new ProcessStartInfo(processPath);
        startInfo.ArgumentList.Add(entryAssemblyPath);
        return startInfo;
    }

    if (!string.IsNullOrWhiteSpace(processPath))
    {
        return new ProcessStartInfo(processPath);
    }

    var fallback = new ProcessStartInfo("dotnet");
    fallback.ArgumentList.Add(entryAssemblyPath);
    return fallback;
}

void CleanupShm(string path)
{
    try
    {
        Xproc.ShmUnlink(path);
    }
    catch
    {
        // Best-effort cleanup. On Windows this is a no-op anyway.
    }
}
