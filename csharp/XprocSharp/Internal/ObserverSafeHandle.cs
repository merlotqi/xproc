using Microsoft.Win32.SafeHandles;

namespace XprocSharp.Internal;

internal sealed class ObserverSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public ObserverSafeHandle()
        : base(true)
    {
    }

    internal ObserverSafeHandle(IntPtr handle)
        : this()
    {
        SetHandle(handle);
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.xproc_c_observer_close(handle);
        return true;
    }
}
