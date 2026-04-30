using System.Text;
using XprocSharp;

const ulong SchemaId = 0x2026043002UL;
var path = $"/xproc_csharp_varlen_{Environment.ProcessId}";
var messages = new[] { "hello", "xproc", "variable-length", "messages" };

CleanupShm(path);

try
{
    var created = XprocShm.CreateVarlenChannel(
        path,
        dataCapacity: 32768,
        schemaId: SchemaId);

    using var producer = created.OpenProducer();
    using var consumer = XprocShm.AttachVarlenChannel(path, schemaId: SchemaId).OpenConsumer();

    Exception? receiverFailure = null;
    var receiver = new Thread(() =>
    {
        try
        {
            var expectedIndex = 0;
            while (expectedIndex < messages.Length)
            {
                var payload = consumer.PollCopy();
                if (payload is null)
                {
                    consumer.WaitForData();
                    continue;
                }

                var text = Encoding.UTF8.GetString(payload);
                Console.WriteLine($"recv: {text}");

                if (!string.Equals(text, messages[expectedIndex], StringComparison.Ordinal))
                {
                    throw new InvalidOperationException(
                        $"sequence mismatch, expected \"{messages[expectedIndex]}\" got \"{text}\"");
                }

                expectedIndex += 1;
            }
        }
        catch (Exception ex)
        {
            receiverFailure = ex;
        }
    });

    receiver.Start();

    foreach (var message in messages)
    {
        producer.SendVarlen(Encoding.UTF8.GetBytes(message));
        Thread.Sleep(8);
    }

    receiver.Join();

    if (receiverFailure is not null)
    {
        throw new InvalidOperationException("receiver failed", receiverFailure);
    }
}
finally
{
    CleanupShm(path);
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
