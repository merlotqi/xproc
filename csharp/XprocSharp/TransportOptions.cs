namespace XprocSharp;

public sealed record class TransportOptions
{
    public XprocBackend Backend { get; init; } = XprocBackend.SharedMemory;

    public string? Path { get; init; }

    public nuint ShmSize { get; init; } = Xproc.InferExistingShmSize;

    public uint ItemSize { get; init; }

    public uint DataAlign { get; init; }

    public ulong SchemaId { get; init; }

    public ulong CreatorTimestampNs { get; init; }

    public ulong CreatorFlags { get; init; }

    public bool CreateIfMissing { get; init; } = true;

    public XprocChannelType ChannelType { get; init; } = XprocChannelType.Fixed;

    public string? Win32ObjectNamespace { get; init; }

    public string? SocketHost { get; init; }

    public ushort SocketPort { get; init; }

    public bool SocketListen { get; init; }

    public int SocketConnectRetries { get; init; } = 200;

    public int SocketConnectRetryMs { get; init; } = 10;

    internal TransportOptions Copy() =>
        new()
        {
            Backend = Backend,
            Path = Path,
            ShmSize = ShmSize,
            ItemSize = ItemSize,
            DataAlign = DataAlign,
            SchemaId = SchemaId,
            CreatorTimestampNs = CreatorTimestampNs,
            CreatorFlags = CreatorFlags,
            CreateIfMissing = CreateIfMissing,
            ChannelType = ChannelType,
            Win32ObjectNamespace = Win32ObjectNamespace,
            SocketHost = SocketHost,
            SocketPort = SocketPort,
            SocketListen = SocketListen,
            SocketConnectRetries = SocketConnectRetries,
            SocketConnectRetryMs = SocketConnectRetryMs,
        };
}
