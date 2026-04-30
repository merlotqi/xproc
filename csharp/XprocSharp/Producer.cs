using XprocSharp.Internal;

namespace XprocSharp;

public sealed class Producer : IDisposable
{
    private readonly ProducerSafeHandle _handle;

    public Producer(TransportOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var scope = new NativeOptionsScope(options);
        var status = NativeMethods.xproc_c_producer_open(ref scope.Value, out var handle);
        Xproc.ThrowIfError(status, nameof(Producer));
        _handle = new ProducerSafeHandle(handle);
    }

    public void Dispose() => _handle.Dispose();

    public TransportOptions GetOptions()
    {
        var status = NativeMethods.xproc_c_producer_options(DangerousHandle(), out var options);
        Xproc.ThrowIfError(status, $"{nameof(Producer)}.{nameof(GetOptions)}");
        return NativeMethods.FromNativeOptions(options);
    }

    public unsafe void SendFixedBytes(ReadOnlySpan<byte> data)
    {
        if ((ulong)data.Length > uint.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(data), "payload length must fit in uint32");
        }

        fixed (byte* ptr = data)
        {
            var status = NativeMethods.xproc_c_producer_send_fixed_bytes(
                DangerousHandle(),
                (IntPtr)ptr,
                (uint)data.Length);
            Xproc.ThrowIfError(status, $"{nameof(Producer)}.{nameof(SendFixedBytes)}");
        }
    }

    public unsafe void SendFixedSized(ReadOnlySpan<byte> data)
    {
        if ((ulong)data.Length > uint.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(data), "payload length must fit in uint32");
        }

        fixed (byte* ptr = data)
        {
            var status = NativeMethods.xproc_c_producer_send_fixed_sized(
                DangerousHandle(),
                (IntPtr)ptr,
                (uint)data.Length);
            Xproc.ThrowIfError(status, $"{nameof(Producer)}.{nameof(SendFixedSized)}");
        }
    }

    public unsafe void SendVarlen(ReadOnlySpan<byte> data)
    {
        if ((ulong)data.Length > uint.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(data), "payload length must fit in uint32");
        }

        fixed (byte* ptr = data)
        {
            var status = NativeMethods.xproc_c_producer_send_varlen(
                DangerousHandle(),
                (IntPtr)ptr,
                (uint)data.Length);
            Xproc.ThrowIfError(status, $"{nameof(Producer)}.{nameof(SendVarlen)}");
        }
    }

    public ushort GetSocketPort()
    {
        var status = NativeMethods.xproc_c_producer_socket_port(DangerousHandle(), out var port);
        Xproc.ThrowIfError(status, $"{nameof(Producer)}.{nameof(GetSocketPort)}");
        return port;
    }

    private IntPtr DangerousHandle()
    {
        if (_handle.IsClosed || _handle.IsInvalid)
        {
            throw new ObjectDisposedException(nameof(Producer));
        }

        return _handle.DangerousGetHandle();
    }
}
