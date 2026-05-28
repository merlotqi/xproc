using System.Runtime.InteropServices;

namespace XprocSharp.Internal;

internal static class NativeMethods
{
    internal const string LibraryName = "xproc_c";

    static NativeMethods()
    {
        NativeLibraryBootstrapper.EnsureRegistered();
    }

    internal static void EnsureLoaded()
    {
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeOptions
    {
        public XprocBackend backend;
        public IntPtr path;
        public UIntPtr shm_size;
        public uint item_size;
        public uint data_align;
        public ulong schema_id;
        public ulong creator_timestamp_ns;
        public ulong creator_flags;
        public int create_if_missing;
        public XprocChannelType channel_type;
        public IntPtr win32_object_namespace;
        public IntPtr socket_host;
        public ushort socket_port;
        public int socket_listen;
        public int socket_connect_retries;
        public int socket_connect_retry_ms;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeSnapshot
    {
        public ulong write_pos;
        public ulong read_pos;
        public uint commit_seq;
        public uint read_wake_seq;
        public uint attach_count;
        public int producer_pid;
    }

    internal static TransportOptions FromNativeOptions(NativeOptions nativeOptions) =>
        new()
        {
            Backend = nativeOptions.backend,
            Path = PtrToStringUtf8(nativeOptions.path),
            ShmSize = checked((nuint)nativeOptions.shm_size.ToUInt64()),
            ItemSize = nativeOptions.item_size,
            DataAlign = nativeOptions.data_align,
            SchemaId = nativeOptions.schema_id,
            CreatorTimestampNs = nativeOptions.creator_timestamp_ns,
            CreatorFlags = nativeOptions.creator_flags,
            CreateIfMissing = nativeOptions.create_if_missing != 0,
            ChannelType = nativeOptions.channel_type,
            Win32ObjectNamespace = PtrToStringUtf8(nativeOptions.win32_object_namespace),
            SocketHost = PtrToStringUtf8(nativeOptions.socket_host),
            SocketPort = nativeOptions.socket_port,
            SocketListen = nativeOptions.socket_listen != 0,
            SocketConnectRetries = nativeOptions.socket_connect_retries,
            SocketConnectRetryMs = nativeOptions.socket_connect_retry_ms,
        };

    internal static string? PtrToStringUtf8(IntPtr ptr)
    {
        return ptr == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(ptr);
    }

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void xproc_c_options_init(out NativeOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr xproc_c_shm_size_for_data_capacity(UIntPtr data_capacity);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr xproc_c_shm_data_capacity_for_size(UIntPtr shm_size);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_shm_read_existing_options(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? win32_object_namespace,
        out NativeOptions out_options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr xproc_c_status_string(XprocStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr xproc_c_layout_error_string(XprocLayoutError error);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr xproc_c_version_string();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int xproc_c_current_process_id();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_validate_options_for(XprocEndpointKind kind, ref NativeOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_shm_unlink([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr xproc_c_last_error_message();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocLayoutError xproc_c_last_layout_error();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_producer_open(ref NativeOptions options, out IntPtr out_producer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void xproc_c_producer_close(IntPtr producer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_producer_options(IntPtr producer, out NativeOptions out_options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_producer_send_fixed_bytes(IntPtr producer, IntPtr data, uint payload_len);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_producer_send_fixed_sized(IntPtr producer, IntPtr data, uint byte_length);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_producer_send_varlen(IntPtr producer, IntPtr data, uint len);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_producer_socket_port(IntPtr producer, out ushort out_port);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_consumer_open(ref NativeOptions options, out IntPtr out_consumer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void xproc_c_consumer_close(IntPtr consumer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_consumer_options(IntPtr consumer, out NativeOptions out_options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_consumer_pending_len(IntPtr consumer, out uint out_len);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_consumer_poll_copy(
        IntPtr consumer,
        IntPtr buffer,
        uint buffer_capacity,
        out uint out_len);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_consumer_wait(IntPtr consumer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_consumer_socket_port(IntPtr consumer, out ushort out_port);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_observer_open(ref NativeOptions options, out IntPtr out_observer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void xproc_c_observer_close(IntPtr observer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_observer_options(IntPtr observer, out NativeOptions out_options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_observer_snapshot(IntPtr observer, out NativeSnapshot out_snapshot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern XprocStatus xproc_c_observer_peek_copy(
        IntPtr observer,
        IntPtr buffer,
        uint buffer_capacity,
        out uint out_len);
}
