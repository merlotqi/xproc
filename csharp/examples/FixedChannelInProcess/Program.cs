using XprocSharp;

var path = $"/xproc_csharp_demo_{Environment.ProcessId}";
var value = 0x12345678;

try
{
    Xproc.ShmUnlink(path);
}
catch
{
    // Best-effort cleanup. On Windows this is a no-op anyway.
}

var created = XprocShm.CreateFixedChannel(
    path,
    itemSize: sizeof(int),
    dataCapacity: 4096,
    schemaId: 0x1234);

using var producer = created.OpenProducer();
using var consumer = XprocShm.AttachFixedChannel(path, schemaId: 0x1234).OpenConsumer();

producer.SendFixedSized(BitConverter.GetBytes(value));
var payload = consumer.PollCopy() ?? throw new InvalidOperationException("expected payload");

if (payload.Length != sizeof(int))
{
    throw new InvalidOperationException($"expected {sizeof(int)} bytes, got {payload.Length}");
}

var actual = BitConverter.ToInt32(payload, 0);
Console.WriteLine($"Received 0x{actual:X8} from {path}");

try
{
    Xproc.ShmUnlink(path);
}
catch
{
    // Best-effort cleanup.
}
