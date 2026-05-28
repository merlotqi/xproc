using Microsoft.Win32.SafeHandles;

namespace XprocSharp.Internal;

internal sealed class ProducerSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public ProducerSafeHandle()
        : base(true)
    {
    }

    internal ProducerSafeHandle(IntPtr handle)
        : this()
    {
        SetHandle(handle);
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.xproc_c_producer_close(handle);
        return true;
    }
}
