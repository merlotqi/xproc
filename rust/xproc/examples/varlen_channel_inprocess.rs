use std::thread;
use std::time::Duration;

use xproc::{shm_unlink, ChannelType, Consumer, Options, Producer};

fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let path = "/xproc_example_varlen_inprocess_rust";
    let _ = shm_unlink(path);

    {
        let producer_options = Options::default()
            .with_path(path)
            .with_channel_type(ChannelType::Varlen)
            .with_shm_size_for_data_capacity(32 * 1024);
        let consumer_options = producer_options
            .clone()
            .with_infer_existing_shm_size()
            .with_create_if_missing(false);

        let producer = Producer::open(&producer_options)?;
        let expected_messages = vec![
            "hello".to_owned(),
            "xproc".to_owned(),
            "variable-length".to_owned(),
            "messages".to_owned(),
        ];
        let expected_in_thread = expected_messages.clone();
        let consumer_path = path.to_owned();
        let consumer_options_for_thread = consumer_options.clone();

        let worker = thread::spawn(
            move || -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
                let consumer = Consumer::open(&consumer_options_for_thread).or_else(|_| {
                    Consumer::open(
                        &Options::default()
                            .with_path(&consumer_path)
                            .with_channel_type(ChannelType::Varlen)
                            .with_infer_existing_shm_size()
                            .with_create_if_missing(false),
                    )
                })?;
                for expected in expected_in_thread {
                    loop {
                        match consumer.poll_copy()? {
                            Some(payload) => {
                                let value = String::from_utf8(payload)?;
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

        for message in &expected_messages {
            producer.send_varlen(message.as_bytes())?;
            thread::sleep(Duration::from_millis(8));
        }

        worker.join().expect("worker thread panicked")?;
    }

    shm_unlink(path)?;
    Ok(())
}
