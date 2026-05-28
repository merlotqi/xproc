using XprocSharp;

const ulong SchemaId = 0x2026043003UL;
const uint Value = 0x42U;

var path = $"/xproc_csharp_observer_{Environment.ProcessId}";
CleanupShm(path);

try
{
    var created = XprocShm.CreateFixedChannel(
        path,
        itemSize: sizeof(uint),
        dataCapacity: 16384,
        schemaId: SchemaId);

    using var producer = created.OpenProducer();
    using var consumer = created.OpenConsumer();
    using var observer = created.OpenObserver();

    producer.SendFixedSized(BitConverter.GetBytes(Value));

    var peeked = WaitForPeek(observer);
    var observed = ReadUInt32(peeked, "observer");
    Console.WriteLine($"observer sees: {observed}");

    var consumed = WaitForPoll(consumer);
    var actual = ReadUInt32(consumed, "consumer");
    Console.WriteLine($"consumer got: {actual}, len={consumed.Length}");

    var snapshot = observer.GetSnapshot();
    Console.WriteLine(
        $"snapshot write_pos={snapshot.WritePos} read_pos={snapshot.ReadPos} attach_count={snapshot.AttachCount}");
}
finally
{
    CleanupShm(path);
}

static byte[] WaitForPeek(Observer observer)
{
    for (var attempt = 0; attempt < 200; attempt += 1)
    {
        var payload = observer.PeekCopy();
        if (payload is not null)
        {
            return payload;
        }

        Thread.Sleep(1);
    }

    throw new TimeoutException("observer did not receive a payload");
}

static byte[] WaitForPoll(Consumer consumer)
{
    while (true)
    {
        var payload = consumer.PollCopy();
        if (payload is not null)
        {
            return payload;
        }

        consumer.WaitForData();
    }
}

static uint ReadUInt32(byte[] payload, string source)
{
    if (payload.Length != sizeof(uint))
    {
        throw new InvalidOperationException($"{source} expected {sizeof(uint)} bytes, got {payload.Length}");
    }

    return BitConverter.ToUInt32(payload, 0);
}

static void CleanupShm(string path)
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
