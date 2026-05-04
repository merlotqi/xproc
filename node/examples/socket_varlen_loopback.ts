const timers = require("node:timers/promises");

const xproc = require("../index.js") as XprocModule;

const delay = timers.setTimeout as (ms: number) => Promise<void>;

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
  const listener = xproc.socket.listenVarlen({
    host: "127.0.0.1",
    port: 0,
  });
  const consumer = listener.openConsumer();
  const producer = xproc.socket.connectVarlen({
    host: "127.0.0.1",
    port: consumer.socketPort(),
  }).openProducer();

  const messages = ["hello", "from", "@merlotqi/xproc", "socket"];
  let received = 0;

  try {
    for (const message of messages) {
      producer.sendVarlen(message);
      const payload = await waitForPoll(consumer);
      const text = Buffer.from(payload).toString("utf8");
      console.log(`recv: ${text}`);
      if (text !== messages[received]) {
        throw new Error(`sequence mismatch, expected ${messages[received]} got ${text}`);
      }
      received += 1;
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
