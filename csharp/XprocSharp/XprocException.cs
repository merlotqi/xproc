namespace XprocSharp;

public sealed class XprocException : Exception
{
    public XprocException(string message, XprocStatus status, XprocLayoutError layoutError = XprocLayoutError.None)
        : base(message)
    {
        Status = status;
        LayoutError = layoutError;
    }

    public XprocStatus Status { get; }

    public XprocLayoutError LayoutError { get; }
}
