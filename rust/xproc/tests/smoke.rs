use xproc::{read_existing_shm_options, shm_unlink, ChannelType, Consumer, Observer, Options, Producer};

#[test]
fn fixed_shared_memory_round_trip() {
    let path = format!("/xproc_rust_smoke_{}", std::process::id());
    let _ = shm_unlink(&path);

    let producer_options = Options::default()
        .with_path(&path)
        .with_channel_type(ChannelType::Fixed)
        .with_item_size(std::mem::size_of::<u32>() as u32)
        .with_shm_size_for_data_capacity(8192);

    let consumer_options = producer_options
        .clone()
        .with_infer_existing_shm_size()
        .with_create_if_missing(false);

    let producer = Producer::open(&producer_options).expect("producer opens");
    let consumer = Consumer::open(&consumer_options).expect("consumer opens");

    producer
        .send_fixed_sized(&0x1234_5678u32.to_ne_bytes())
        .expect("send succeeds");

    let payload = consumer.poll_copy().expect("poll succeeds").expect("message available");
    assert_eq!(payload, 0x1234_5678u32.to_ne_bytes());

    shm_unlink(&path).expect("cleanup succeeds");
}

#[test]
fn read_existing_shm_options_infers_creator_metadata() {
    let path = format!("/xproc_rust_infer_{}", std::process::id());
    let _ = shm_unlink(&path);

    let producer_options = Options::default()
        .with_path(&path)
        .with_channel_type(ChannelType::Fixed)
        .with_item_size(std::mem::size_of::<u32>() as u32)
        .with_schema_id(0x1234)
        .with_creator_timestamp_ns(0x1122_3344_5566_7788)
        .with_creator_flags(0x8877_6655_4433_2211)
        .with_shm_size_for_data_capacity(8192);

    let _producer = Producer::open(&producer_options).expect("producer opens");
    let inferred = read_existing_shm_options(&path, None).expect("options inferred");

    assert_eq!(inferred.channel_type, ChannelType::Fixed);
    assert_eq!(inferred.item_size, std::mem::size_of::<u32>() as u32);
    assert_eq!(inferred.schema_id, 0x1234);
    assert_eq!(inferred.creator_timestamp_ns, 0x1122_3344_5566_7788);
    assert_eq!(inferred.creator_flags, 0x8877_6655_4433_2211);
    assert!(!inferred.create_if_missing);

    shm_unlink(&path).expect("cleanup succeeds");
}

#[test]
fn varlen_round_trip_preserves_message_bytes() {
    let path = format!("/xproc_rust_varlen_{}", std::process::id());
    let _ = shm_unlink(&path);

    let producer_options = Options::default()
        .with_path(&path)
        .with_channel_type(ChannelType::Varlen)
        .with_shm_size_for_data_capacity(16 * 1024);
    let consumer_options = producer_options
        .clone()
        .with_infer_existing_shm_size()
        .with_create_if_missing(false);

    let producer = Producer::open(&producer_options).expect("producer opens");
    let consumer = Consumer::open(&consumer_options).expect("consumer opens");
    let expected = b"hello from rust example";

    producer.send_varlen(expected).expect("send succeeds");

    let payload = consumer.poll_copy().expect("poll succeeds").expect("message available");
    assert_eq!(payload, expected);

    shm_unlink(&path).expect("cleanup succeeds");
}

#[test]
fn observer_peek_does_not_consume_message() {
    let path = format!("/xproc_rust_observer_{}", std::process::id());
    let _ = shm_unlink(&path);

    let producer_options = Options::default()
        .with_path(&path)
        .with_channel_type(ChannelType::Fixed)
        .with_item_size(std::mem::size_of::<u32>() as u32)
        .with_shm_size_for_data_capacity(8192);
    let attach_options = producer_options
        .clone()
        .with_infer_existing_shm_size()
        .with_create_if_missing(false);

    let producer = Producer::open(&producer_options).expect("producer opens");
    let consumer = Consumer::open(&attach_options).expect("consumer opens");
    let observer = Observer::open(&attach_options).expect("observer opens");
    let expected = 0x42u32.to_ne_bytes();

    producer.send_fixed_sized(&expected).expect("send succeeds");

    let peeked = observer.peek_copy().expect("peek succeeds").expect("message visible");
    assert_eq!(peeked, expected);

    let consumed = consumer.poll_copy().expect("poll succeeds").expect("message available");
    assert_eq!(consumed, expected);

    let snapshot = observer.snapshot().expect("snapshot succeeds");
    assert!(snapshot.attach_count >= 1);

    shm_unlink(&path).expect("cleanup succeeds");
}
