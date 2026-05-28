using System.Runtime.InteropServices;

namespace XprocSharp.Internal;

internal sealed class NativeOptionsScope : IDisposable
{
    private readonly List<IntPtr> _allocated = [];

    public NativeOptionsScope(TransportOptions options)
    {
        NativeMethods.xproc_c_options_init(out Value);
        Value.backend = options.Backend;
        Value.path = AllocateUtf8(options.Path);
        Value.shm_size = (UIntPtr)options.ShmSize;
        Value.item_size = options.ItemSize;
        Value.data_align = options.DataAlign;
        Value.schema_id = options.SchemaId;
        Value.creator_timestamp_ns = options.CreatorTimestampNs;
        Value.creator_flags = options.CreatorFlags;
        Value.create_if_missing = options.CreateIfMissing ? 1 : 0;
        Value.channel_type = options.ChannelType;
        Value.win32_object_namespace = AllocateUtf8(options.Win32ObjectNamespace);
        Value.socket_host = AllocateUtf8(options.SocketHost);
        Value.socket_port = options.SocketPort;
        Value.socket_listen = options.SocketListen ? 1 : 0;
        Value.socket_connect_retries = options.SocketConnectRetries;
        Value.socket_connect_retry_ms = options.SocketConnectRetryMs;
    }

    public NativeMethods.NativeOptions Value;

    public void Dispose()
    {
        foreach (var ptr in _allocated)
        {
            Marshal.FreeCoTaskMem(ptr);
        }
    }

    private IntPtr AllocateUtf8(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return IntPtr.Zero;
        }

        var ptr = Marshal.StringToCoTaskMemUTF8(value);
        _allocated.Add(ptr);
        return ptr;
    }
}
