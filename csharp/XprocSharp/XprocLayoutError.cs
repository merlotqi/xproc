namespace XprocSharp;

public enum XprocLayoutError
{
    None = 0,
    NotAttached = 1,
    BadMagic = 2,
    NotReadyTimeout = 3,
    VersionMismatch = 4,
    HeaderSizeMismatch = 5,
    LayoutTypeMismatch = 6,
    FixedItemSizeMismatch = 7,
    SchemaIdMismatch = 8,
    AlignmentInvalid = 9,
    CapacityInsufficient = 10,
}
