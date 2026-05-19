use xproc::{shm_unlink, ChannelType, Consumer, Observer, Options, Producer};

fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let path = "/xproc_example_observer_rust";
    let _ = shm_unlink(path);

    {
        let producer_options = Options::default()
            .with_path(path)
            .with_channel_type(ChannelType::Fixed)
            .with_item_size(std::mem::size_of::<u32>() as u32)
            .with_shm_size_for_data_capacity(16 * 1024);
        let attach_options = producer_options
            .clone()
            .with_infer_existing_shm_size()
            .with_create_if_missing(false);

        let producer = Producer::open(&producer_options)?;
        let consumer = Consumer::open(&attach_options)?;
        let observer = Observer::open(&attach_options)?;

        producer.send_fixed_sized(&0x42u32.to_ne_bytes())?;

        let peeked = observer.peek_copy()?.expect("message visible to observer");
        let peeked_value = decode_u32(&peeked)?;
        println!("observer sees: {peeked_value}");

        let consumed = consumer.poll_copy()?.expect("message visible to consumer");
        let consumed_value = decode_u32(&consumed)?;
        println!("consumer got: {consumed_value}, len={}", consumed.len());

        let snapshot = observer.snapshot()?;
        println!(
            "snapshot write_pos={} read_pos={} attach_count={}",
            snapshot.write_pos, snapshot.read_pos, snapshot.attach_count
        );
    }

    shm_unlink(path)?;
    Ok(())
}

fn decode_u32(payload: &[u8]) -> Result<u32, Box<dyn std::error::Error + Send + Sync>> {
    if payload.len() != std::mem::size_of::<u32>() {
        return Err(format!("expected 4 bytes, got {}", payload.len()).into());
    }

    let mut bytes = [0u8; 4];
    bytes.copy_from_slice(payload);
    Ok(u32::from_ne_bytes(bytes))
}
