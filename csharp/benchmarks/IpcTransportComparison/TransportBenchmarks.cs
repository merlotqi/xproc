using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.IO.Pipes;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using XprocSharp;

namespace XprocSharp.Benchmarks.IpcTransportComparison;

internal sealed class XprocTransportPollCopyBenchmark : ITransportBenchmark
{
    public string Name => "xproc_fixed_pollcopy";

    public TransportRunResult Run(BenchmarkSpec spec)
    {
        var path = $"/xproc_csharp_bench_{Environment.ProcessId}_{Guid.NewGuid():N}";
        var payload = TransportBenchmarkHelpers.CreatePayload(spec.PayloadSize);

        TransportBenchmarkHelpers.CleanupShm(path);

        try
        {
            var channel = XprocShm.CreateFixedChannel(
                path,
                itemSize: (uint)spec.PayloadSize,
                dataCapacity: 1024 * 1024,
                schemaId: 0x2026043005UL);

            using var producer = channel.OpenProducer();
            using var consumer = XprocShm.AttachFixedChannel(path, schemaId: 0x2026043005UL).OpenConsumer();

            return TransportBenchmarkHelpers.RunHandoffBenchmark(
                spec,
                () => producer.SendFixedSized(payload),
                () =>
                {
                    var spinner = new SpinWait();
                    while (true)
                    {
                        var data = consumer.PollCopy();
                        if (data is not null)
                        {
                            return TransportBenchmarkHelpers.Checksum(data);
                        }

                        spinner.SpinOnce();
                    }
                });
        }
        finally
        {
            TransportBenchmarkHelpers.CleanupShm(path);
        }
    }
}

internal sealed class XprocTransportSpanBenchmark : ITransportBenchmark
{
    public string Name => "xproc_fixed_span";

    public TransportRunResult Run(BenchmarkSpec spec)
    {
        var path = $"/xproc_csharp_bench_{Environment.ProcessId}_{Guid.NewGuid():N}";
        var payload = TransportBenchmarkHelpers.CreatePayload(spec.PayloadSize);
        var receiveBuffer = GC.AllocateUninitializedArray<byte>(spec.PayloadSize);

        TransportBenchmarkHelpers.CleanupShm(path);

        try
        {
            var channel = XprocShm.CreateFixedChannel(
                path,
                itemSize: (uint)spec.PayloadSize,
                dataCapacity: 1024 * 1024,
                schemaId: 0x2026043005UL);

            using var producer = channel.OpenProducer();
            using var consumer = XprocShm.AttachFixedChannel(path, schemaId: 0x2026043005UL).OpenConsumer();

            return TransportBenchmarkHelpers.RunHandoffBenchmark(
                spec,
                () => producer.SendFixedSized(payload),
                () =>
                {
                    var spinner = new SpinWait();
                    while (true)
                    {
                        if (consumer.TryPollCopy(receiveBuffer, out var bytesWritten))
                        {
                            return TransportBenchmarkHelpers.Checksum(receiveBuffer.AsSpan(0, bytesWritten));
                        }

                        spinner.SpinOnce();
                    }
                });
        }
        finally
        {
            TransportBenchmarkHelpers.CleanupShm(path);
        }
    }
}

internal sealed class NamedPipeTransportBenchmark : ITransportBenchmark
{
    public string Name => "named_pipe";

    public TransportRunResult Run(BenchmarkSpec spec)
    {
        var payload = TransportBenchmarkHelpers.CreatePayload(spec.PayloadSize);
        var pipeName = $"xproc-csharp-bench-{Guid.NewGuid():N}";

        using var server = new NamedPipeServerStream(
            pipeName,
            PipeDirection.Out,
            1,
            PipeTransmissionMode.Byte,
            PipeOptions.None,
            outBufferSize: spec.PayloadSize * 64,
            inBufferSize: spec.PayloadSize * 64);
        using var client = new NamedPipeClientStream(".", pipeName, PipeDirection.In, PipeOptions.None);

        var connectTask = client.ConnectAsync();
        server.WaitForConnection();
        connectTask.GetAwaiter().GetResult();

        return TransportBenchmarkHelpers.RunHandoffBenchmark(
            spec,
            () => server.Write(payload),
            () =>
            {
                var buffer = new byte[spec.PayloadSize];
                TransportBenchmarkHelpers.ReadExactly(client, buffer);
                return TransportBenchmarkHelpers.Checksum(buffer);
            },
            afterProduce: server.Flush);
    }
}

internal sealed class AnonymousPipeTransportBenchmark : ITransportBenchmark
{
    public string Name => "anonymous_pipe";

    public TransportRunResult Run(BenchmarkSpec spec)
    {
        var payload = TransportBenchmarkHelpers.CreatePayload(spec.PayloadSize);

        using var server = new AnonymousPipeServerStream(PipeDirection.Out, HandleInheritability.None);
        using var client = new AnonymousPipeClientStream(PipeDirection.In, server.GetClientHandleAsString());

        return TransportBenchmarkHelpers.RunHandoffBenchmark(
            spec,
            () => server.Write(payload),
            () =>
            {
                var buffer = new byte[spec.PayloadSize];
                TransportBenchmarkHelpers.ReadExactly(client, buffer);
                return TransportBenchmarkHelpers.Checksum(buffer);
            },
            afterProduce: server.Flush);
    }
}

internal sealed class MemoryMappedFileTransportBenchmark : ITransportBenchmark
{
    private const int Empty = 0;
    private const int Ready = 1;
    private const int HeaderBytes = 64;

    public string Name => "mmf_slot";

    public unsafe TransportRunResult Run(BenchmarkSpec spec)
    {
        var slotBytes = HeaderBytes + spec.PayloadSize;
        using var mapping = MemoryMappedFile.CreateNew(null, slotBytes);
        using var writer = mapping.CreateViewAccessor(0, slotBytes, MemoryMappedFileAccess.ReadWrite);
        using var reader = mapping.CreateViewAccessor(0, slotBytes, MemoryMappedFileAccess.ReadWrite);

        byte* writerPtr = null;
        byte* readerPtr = null;

        writer.SafeMemoryMappedViewHandle.AcquirePointer(ref writerPtr);
        reader.SafeMemoryMappedViewHandle.AcquirePointer(ref readerPtr);

        try
        {
            var writerState = (int*)writerPtr;
            var readerState = (int*)readerPtr;
            var writerPayload = writerPtr + HeaderBytes;
            var readerPayload = readerPtr + HeaderBytes;
            var payload = TransportBenchmarkHelpers.CreatePayload(spec.PayloadSize);
            var readBuffer = new byte[spec.PayloadSize];

            Volatile.Write(ref *writerState, Empty);

            return TransportBenchmarkHelpers.RunHandoffBenchmark(
                spec,
                () =>
                {
                    var spinner = new SpinWait();
                    while (Volatile.Read(ref *writerState) != Empty)
                    {
                        spinner.SpinOnce();
                    }

                    Marshal.Copy(payload, 0, (IntPtr)writerPayload, payload.Length);
                    Volatile.Write(ref *writerState, Ready);
                },
                () =>
                {
                    var spinner = new SpinWait();
                    while (Volatile.Read(ref *readerState) != Ready)
                    {
                        spinner.SpinOnce();
                    }

                    Marshal.Copy((IntPtr)readerPayload, readBuffer, 0, readBuffer.Length);
                    Volatile.Write(ref *readerState, Empty);
                    return TransportBenchmarkHelpers.Checksum(readBuffer);
                });
        }
        finally
        {
            if (readerPtr is not null)
            {
                reader.SafeMemoryMappedViewHandle.ReleasePointer();
            }

            if (writerPtr is not null)
            {
                writer.SafeMemoryMappedViewHandle.ReleasePointer();
            }
        }
    }
}

internal static class TransportBenchmarkHelpers
{
    public static void CleanupShm(string path)
    {
        try
        {
            Xproc.ShmUnlink(path);
        }
        catch
        {
            // Best-effort cleanup. On Windows this is a no-op by design.
        }
    }

    public static TransportRunResult RunHandoffBenchmark(
        BenchmarkSpec spec,
        Action produceOne,
        Func<ulong> consumeOne,
        Action? afterProduce = null)
    {
        ulong checksum = 0;
        Exception? consumerFailure = null;
        var consumedCount = 0;

        var consumerThread = new Thread(() =>
        {
            try
            {
                for (var index = 0; index < spec.Iterations; index += 1)
                {
                    checksum ^= consumeOne();
                    Volatile.Write(ref consumedCount, index + 1);
                }
            }
            catch (Exception ex)
            {
                consumerFailure = ex;
            }
        })
        {
            IsBackground = true,
            Priority = ThreadPriority.AboveNormal,
        };

        consumerThread.Start();

        var started = Stopwatch.GetTimestamp();
        for (var index = 0; index < spec.Iterations; index += 1)
        {
            produceOne();

            var spinner = new SpinWait();
            while (Volatile.Read(ref consumedCount) != index + 1)
            {
                if (consumerFailure is not null)
                {
                    ExceptionDispatchInfo.Capture(consumerFailure).Throw();
                }

                spinner.SpinOnce();
            }
        }

        afterProduce?.Invoke();
        consumerThread.Join();
        var elapsed = Stopwatch.GetElapsedTime(started);

        if (consumerFailure is not null)
        {
            ExceptionDispatchInfo.Capture(consumerFailure).Throw();
        }

        return new TransportRunResult(elapsed, checksum);
    }

    public static byte[] CreatePayload(int payloadSize)
    {
        var payload = GC.AllocateUninitializedArray<byte>(payloadSize);
        for (var index = 0; index < payload.Length; index += 1)
        {
            payload[index] = unchecked((byte)(index * 31 + 17));
        }

        return payload;
    }

    public static ulong Checksum(ReadOnlySpan<byte> data)
    {
        if (data.IsEmpty)
        {
            return 0;
        }

        return ((ulong)data[0] << 32) | data[^1];
    }

    public static void ReadExactly(Stream stream, byte[] buffer)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = stream.Read(buffer, offset, buffer.Length - offset);
            if (read == 0)
            {
                throw new EndOfStreamException("unexpected end of stream");
            }

            offset += read;
        }
    }
}
