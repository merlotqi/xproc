using XprocSharp.Internal;

namespace XprocSharp;

public static class Xproc
{
    public const nuint InferExistingShmSize = 0;

    static Xproc()
    {
        NativeMethods.EnsureLoaded();
    }

    public static nuint ShmSizeForDataCapacity(nuint dataCapacity)
    {
        return checked((nuint)NativeMethods.xproc_c_shm_size_for_data_capacity((UIntPtr)dataCapacity).ToUInt64());
    }

    public static nuint ShmDataCapacityForSize(nuint shmSize)
    {
        return checked((nuint)NativeMethods.xproc_c_shm_data_capacity_for_size((UIntPtr)shmSize).ToUInt64());
    }

    public static string StatusString(XprocStatus status)
    {
        return NativeMethods.PtrToStringUtf8(NativeMethods.xproc_c_status_string(status)) ?? status.ToString();
    }

    public static string LayoutErrorString(XprocLayoutError layoutError)
    {
        return NativeMethods.PtrToStringUtf8(NativeMethods.xproc_c_layout_error_string(layoutError)) ??
               layoutError.ToString();
    }

    public static string VersionString()
    {
        return NativeMethods.PtrToStringUtf8(NativeMethods.xproc_c_version_string()) ?? string.Empty;
    }

    public static int CurrentProcessId()
    {
        return NativeMethods.xproc_c_current_process_id();
    }

    public static void ShmUnlink(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var status = NativeMethods.xproc_c_shm_unlink(path);
        ThrowIfError(status, nameof(ShmUnlink));
    }

    public static void ValidateOptionsFor(XprocEndpointKind kind, TransportOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        using var scope = new NativeOptionsScope(options);
        var status = NativeMethods.xproc_c_validate_options_for(kind, ref scope.Value);
        ThrowIfError(status, nameof(ValidateOptionsFor));
    }

    public static TransportOptions ReadExistingShmOptions(string path, string? win32ObjectNamespace = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var status = NativeMethods.xproc_c_shm_read_existing_options(path, win32ObjectNamespace, out var nativeOptions);
        ThrowIfError(status, nameof(ReadExistingShmOptions));
        return NativeMethods.FromNativeOptions(nativeOptions);
    }

    internal static void ThrowIfError(XprocStatus status, string context)
    {
        if (status == XprocStatus.Ok)
        {
            return;
        }

        var details = NativeMethods.PtrToStringUtf8(NativeMethods.xproc_c_last_error_message());
        var message = string.IsNullOrWhiteSpace(details) ? context : $"{context}: {details}";
        throw new XprocException(message, status, NativeMethods.xproc_c_last_layout_error());
    }

    internal static XprocException CreateManagedLayoutException(string context, XprocLayoutError layoutError, string detail)
    {
        return new XprocException($"{context}: {detail}", XprocStatus.LayoutError, layoutError);
    }
}
