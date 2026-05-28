using XprocSharp.Internal;

namespace XprocSharp;

public sealed class Observer : IDisposable
{
    private readonly ObserverSafeHandle _handle;

    public Observer(TransportOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var scope = new NativeOptionsScope(options);
        var status = NativeMethods.xproc_c_observer_open(ref scope.Value, out var handle);
        Xproc.ThrowIfError(status, nameof(Observer));
        _handle = new ObserverSafeHandle(handle);
    }

    public void Dispose() => _handle.Dispose();

    public TransportOptions GetOptions()
    {
        var status = NativeMethods.xproc_c_observer_options(DangerousHandle(), out var options);
        Xproc.ThrowIfError(status, $"{nameof(Observer)}.{nameof(GetOptions)}");
        return NativeMethods.FromNativeOptions(options);
    }

    public XprocSnapshot GetSnapshot()
    {
        var status = NativeMethods.xproc_c_observer_snapshot(DangerousHandle(), out var snapshot);
        Xproc.ThrowIfError(status, $"{nameof(Observer)}.{nameof(GetSnapshot)}");
        return new XprocSnapshot(
            snapshot.write_pos,
            snapshot.read_pos,
            snapshot.commit_seq,
            snapshot.read_wake_seq,
            snapshot.attach_count,
            snapshot.producer_pid);
    }

    public byte[]? PeekCopy()
    {
        var handle = DangerousHandle();
        var status = NativeMethods.xproc_c_observer_peek_copy(handle, IntPtr.Zero, 0, out var requiredLength);
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
            Xproc.ThrowIfError(status, $"{nameof(Observer)}.{nameof(PeekCopy)}");
        }

        var buffer = new byte[requiredLength];
        unsafe
        {
            fixed (byte* ptr = buffer)
            {
                status = NativeMethods.xproc_c_observer_peek_copy(handle, (IntPtr)ptr, requiredLength, out requiredLength);
            }
        }

        Xproc.ThrowIfError(status, $"{nameof(Observer)}.{nameof(PeekCopy)}");
        return buffer;
    }

    private IntPtr DangerousHandle()
    {
        if (_handle.IsClosed || _handle.IsInvalid)
        {
            throw new ObjectDisposedException(nameof(Observer));
        }

        return _handle.DangerousGetHandle();
    }
}
