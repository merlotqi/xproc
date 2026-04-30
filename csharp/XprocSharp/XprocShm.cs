namespace XprocSharp;

public static class XprocShm
{
    public static ShmChannelEndpoints CreateFixedChannel(
        string path,
        uint itemSize,
        nuint dataCapacity,
        uint dataAlign = 0,
        ulong schemaId = 0,
        ulong creatorTimestampNs = 0,
        ulong creatorFlags = 0,
        string? win32ObjectNamespace = "Local")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var options = new TransportOptions
        {
            Path = path,
            ShmSize = Xproc.ShmSizeForDataCapacity(dataCapacity),
            ItemSize = itemSize,
            DataAlign = dataAlign,
            SchemaId = schemaId,
            CreatorTimestampNs = creatorTimestampNs,
            CreatorFlags = creatorFlags,
            CreateIfMissing = true,
            ChannelType = XprocChannelType.Fixed,
            Win32ObjectNamespace = win32ObjectNamespace,
        };

        Xproc.ValidateOptionsFor(XprocEndpointKind.Producer, options);
        Xproc.ValidateOptionsFor(XprocEndpointKind.Consumer, options);
        return new ShmChannelEndpoints(options);
    }

    public static ShmChannelEndpoints CreateVarlenChannel(
        string path,
        nuint dataCapacity,
        uint dataAlign = 0,
        ulong schemaId = 0,
        ulong creatorTimestampNs = 0,
        ulong creatorFlags = 0,
        string? win32ObjectNamespace = "Local")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var options = new TransportOptions
        {
            Path = path,
            ShmSize = Xproc.ShmSizeForDataCapacity(dataCapacity),
            DataAlign = dataAlign,
            SchemaId = schemaId,
            CreatorTimestampNs = creatorTimestampNs,
            CreatorFlags = creatorFlags,
            CreateIfMissing = true,
            ChannelType = XprocChannelType.Varlen,
            Win32ObjectNamespace = win32ObjectNamespace,
        };

        Xproc.ValidateOptionsFor(XprocEndpointKind.Producer, options);
        Xproc.ValidateOptionsFor(XprocEndpointKind.Consumer, options);
        return new ShmChannelEndpoints(options);
    }

    public static ShmChannelEndpoints AttachFixedChannel(
        string path,
        ulong? schemaId = null,
        string? win32ObjectNamespace = "Local")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var inferred = Xproc.ReadExistingShmOptions(path, win32ObjectNamespace);
        if (inferred.ChannelType != XprocChannelType.Fixed)
        {
            throw Xproc.CreateManagedLayoutException(
                nameof(AttachFixedChannel),
                XprocLayoutError.LayoutTypeMismatch,
                "expected a fixed shared-memory channel");
        }

        if (schemaId.HasValue && inferred.SchemaId != schemaId.Value)
        {
            throw Xproc.CreateManagedLayoutException(
                nameof(AttachFixedChannel),
                XprocLayoutError.SchemaIdMismatch,
                "schema_id mismatch");
        }

        var options = inferred.Copy() with
        {
            Path = path,
            SchemaId = schemaId ?? inferred.SchemaId,
            CreateIfMissing = false,
            ChannelType = XprocChannelType.Fixed,
            Win32ObjectNamespace = win32ObjectNamespace ?? inferred.Win32ObjectNamespace,
        };

        Xproc.ValidateOptionsFor(XprocEndpointKind.Producer, options);
        Xproc.ValidateOptionsFor(XprocEndpointKind.Consumer, options);
        return new ShmChannelEndpoints(options);
    }

    public static ShmChannelEndpoints AttachVarlenChannel(
        string path,
        ulong? schemaId = null,
        string? win32ObjectNamespace = "Local")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var inferred = Xproc.ReadExistingShmOptions(path, win32ObjectNamespace);
        if (inferred.ChannelType != XprocChannelType.Varlen)
        {
            throw Xproc.CreateManagedLayoutException(
                nameof(AttachVarlenChannel),
                XprocLayoutError.LayoutTypeMismatch,
                "expected a varlen shared-memory channel");
        }

        if (schemaId.HasValue && inferred.SchemaId != schemaId.Value)
        {
            throw Xproc.CreateManagedLayoutException(
                nameof(AttachVarlenChannel),
                XprocLayoutError.SchemaIdMismatch,
                "schema_id mismatch");
        }

        var options = inferred.Copy() with
        {
            Path = path,
            SchemaId = schemaId ?? inferred.SchemaId,
            CreateIfMissing = false,
            ChannelType = XprocChannelType.Varlen,
            Win32ObjectNamespace = win32ObjectNamespace ?? inferred.Win32ObjectNamespace,
        };

        Xproc.ValidateOptionsFor(XprocEndpointKind.Producer, options);
        Xproc.ValidateOptionsFor(XprocEndpointKind.Consumer, options);
        return new ShmChannelEndpoints(options);
    }
}
