namespace XprocSharp;

public enum XprocStatus
{
    Ok = 0,
    Again = 1,
    BufferTooSmall = 2,
    InvalidArgument = 3,
    LogicError = 4,
    LayoutError = 5,
    RuntimeError = 6,
    NoMemory = 7,
    InternalError = 8,
}
