namespace XprocSharp;

public sealed class ShmChannelEndpoints
{
    private readonly TransportOptions _producerOptions;
    private readonly TransportOptions _consumerOptions;
    private readonly TransportOptions _observerOptions;

    internal ShmChannelEndpoints(TransportOptions baseOptions)
    {
        _producerOptions = baseOptions.Copy();
        _consumerOptions = baseOptions.Copy();
        _observerOptions = baseOptions.Copy() with { CreateIfMissing = false };
    }

    public TransportOptions Options => _producerOptions.Copy();

    public Producer OpenProducer() => new(_producerOptions.Copy());

    public Consumer OpenConsumer() => new(_consumerOptions.Copy());

    public Observer OpenObserver() => new(_observerOptions.Copy());
}
