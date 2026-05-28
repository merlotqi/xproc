use std::thread;
use std::time::Duration;

use xproc::{shm_unlink, ChannelType, Consumer, Options, Producer};

fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let path = "/xproc_example_fixed_inprocess_rust";
    let _ = shm_unlink(path);

    {
        let producer_options = Options::default()
            .with_path(path)
            .with_channel_type(ChannelType::Fixed)
            .with_item_size(std::mem::size_of::<u32>() as u32)
            .with_shm_size_for_data_capacity(16 * 1024);
        let consumer_options = producer_options
            .clone()
            .with_infer_existing_shm_size()
            .with_create_if_missing(false);

        let producer = Producer::open(&producer_options)?;
        let consumer_path = path.to_owned();
        let consumer_options_for_thread = consumer_options.clone();

        let worker = thread::spawn(
            move || -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
                let consumer = Consumer::open(&consumer_options_for_thread).or_else(|_| {
                    Consumer::open(
                        &Options::default()
                            .with_path(&consumer_path)
                            .with_channel_type(ChannelType::Fixed)
                            .with_item_size(std::mem::size_of::<u32>() as u32)
                            .with_infer_existing_shm_size()
                            .with_create_if_missing(false),
                    )
                })?;
                for expected in 1u32..=10 {
                    loop {
                        match consumer.poll_copy()? {
                            Some(payload) => {
                                let value = decode_u32(&payload)?;
                                println!("recv: {value}");
                                assert_eq!(value, expected);
                                break;
                            }
                            None => consumer.wait()?,
                        }
                    }
                }
                Ok(())
            },
        );

        for value in 1u32..=10 {
            producer.send_fixed_sized(&value.to_ne_bytes())?;
            thread::sleep(Duration::from_millis(10));
        }

        worker.join().expect("worker thread panicked")?;
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
