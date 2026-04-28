const timers = require("node:timers/promises");

const xproc = require("../index.js") as XprocModule;

const delay = timers.setTimeout as (ms: number) => Promise<void>;
const MESSAGE_COUNT = 5;

async function waitForPoll(consumer: Consumer): Promise<Uint8Array> {
  while (true) {
    const payload = consumer.pollCopy();
    if (payload !== null) {
      return payload;
    }
    await delay(1);
  }
}

async function main(): Promise<void> {
  const listener = xproc.socket.listenFixed({
    host: "127.0.0.1",
    port: 0,
    itemSize: 4,
  });
  const consumer = listener.openConsumer();
  const producer = xproc.socket.connectFixed({
    host: "127.0.0.1",
    port: consumer.socketPort(),
    itemSize: 4,
  }).openProducer();

  let expected = 1;

  try {
    for (let value = 1; value <= MESSAGE_COUNT; ++value) {
      const payload = Buffer.alloc(4);
      payload.writeInt32LE(value, 0);
      producer.sendFixedSized(payload);
      const message = await waitForPoll(consumer);
      const received = Buffer.from(message).readInt32LE(0);
      console.log(`recv: ${received}`);
      if (received !== expected) {
        throw new Error(`sequence mismatch, expected ${expected} got ${received}`);
      }
      expected += 1;
    }
  } finally {
    producer.close();
    consumer.close();
  }
}

void main().catch((error: unknown) => {
  const message = error instanceof Error ? error.stack ?? error.message : String(error);
  console.error(message);
  process.exit(1);
});
