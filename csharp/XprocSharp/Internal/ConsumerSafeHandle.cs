using Microsoft.Win32.SafeHandles;

namespace XprocSharp.Internal;

internal sealed class ConsumerSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public ConsumerSafeHandle()
        : base(true)
    {
    }

    internal ConsumerSafeHandle(IntPtr handle)
        : this()
    {
        SetHandle(handle);
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.xproc_c_consumer_close(handle);
        return true;
    }
}
