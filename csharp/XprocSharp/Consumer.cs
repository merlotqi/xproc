using XprocSharp.Internal;

namespace XprocSharp;

public sealed class Consumer : IDisposable
{
    private readonly ConsumerSafeHandle _handle;

    public Consumer(TransportOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var scope = new NativeOptionsScope(options);
        var status = NativeMethods.xproc_c_consumer_open(ref scope.Value, out var handle);
        Xproc.ThrowIfError(status, nameof(Consumer));
        _handle = new ConsumerSafeHandle(handle);
    }

    public void Dispose() => _handle.Dispose();

    public TransportOptions GetOptions()
    {
        var status = NativeMethods.xproc_c_consumer_options(DangerousHandle(), out var options);
        Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(GetOptions)}");
        return NativeMethods.FromNativeOptions(options);
    }

    public uint GetPendingLength()
    {
        var status = NativeMethods.xproc_c_consumer_pending_len(DangerousHandle(), out var pendingLength);
        Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(GetPendingLength)}");
        return pendingLength;
    }

    public unsafe bool TryPollCopy(Span<byte> buffer, out int bytesWritten)
    {
        var handle = DangerousHandle();
        uint requiredLength;
        XprocStatus status;

        fixed (byte* ptr = buffer)
        {
            status = NativeMethods.xproc_c_consumer_poll_copy(
                handle,
                buffer.IsEmpty ? IntPtr.Zero : (IntPtr)ptr,
                checked((uint)buffer.Length),
                out requiredLength);
        }

        if (status == XprocStatus.Again)
        {
            bytesWritten = 0;
            return false;
        }

        if (status == XprocStatus.BufferTooSmall)
        {
            throw new ArgumentException(
                $"destination buffer is too small; required at least {requiredLength} bytes",
                nameof(buffer));
        }

        Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(TryPollCopy)}");
        bytesWritten = checked((int)requiredLength);
        return true;
    }

    public byte[]? PollCopy()
    {
        var handle = DangerousHandle();
        var status = NativeMethods.xproc_c_consumer_poll_copy(handle, IntPtr.Zero, 0, out var requiredLength);
        if (status == XprocStatus.Again)
        {
            return null;
        }

        if (status == XprocStatus.Ok)
        {
            return Array.Empty<byte>();
        }

        if (status != XprocStatus.BufferTooSmall)
        {
            Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(PollCopy)}");
        }

        var buffer = new byte[requiredLength];
        unsafe
        {
            fixed (byte* ptr = buffer)
            {
                status = NativeMethods.xproc_c_consumer_poll_copy(handle, (IntPtr)ptr, requiredLength, out requiredLength);
            }
        }

        Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(PollCopy)}");
        return buffer;
    }

    public void WaitForData()
    {
        var status = NativeMethods.xproc_c_consumer_wait(DangerousHandle());
        Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(WaitForData)}");
    }

    public ushort GetSocketPort()
    {
        var status = NativeMethods.xproc_c_consumer_socket_port(DangerousHandle(), out var port);
        Xproc.ThrowIfError(status, $"{nameof(Consumer)}.{nameof(GetSocketPort)}");
        return port;
    }

    private IntPtr DangerousHandle()
    {
        if (_handle.IsClosed || _handle.IsInvalid)
        {
            throw new ObjectDisposedException(nameof(Consumer));
        }

        return _handle.DangerousGetHandle();
    }
}
