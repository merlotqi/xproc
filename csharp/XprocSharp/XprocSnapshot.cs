namespace XprocSharp;

public readonly record struct XprocSnapshot(
    ulong WritePos,
    ulong ReadPos,
    uint CommitSeq,
    uint ReadWakeSeq,
    uint AttachCount,
    int ProducerPid);
