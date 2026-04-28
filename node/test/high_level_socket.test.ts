const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");
const timers = require("node:timers/promises");

const xproc = require("../index.js") as XprocModule;

const delay = timers.setTimeout as (ms: number) => Promise<void>;

async function waitForPoll(consumer: Consumer): Promise<Uint8Array> {
  for (let attempt = 0; attempt < 200; ++attempt) {
    const payload = consumer.pollCopy();
    if (payload !== null) {
      return payload;
    }
    await delay(5);
  }
  throw new Error("timed out waiting for socket payload");
}

test("high-level socket fixed listen/connect roundtrip", async () => {
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

  try {
    producer.sendFixedSized(Buffer.from([9, 8, 7, 6]));
    const payload = await waitForPoll(consumer);
    assert.deepEqual([...Buffer.from(payload)], [9, 8, 7, 6]);
    assert.equal(consumer.options().backend, xproc.BACKEND.socket);
    assert.equal(consumer.options().type, xproc.CHANNEL_TYPE.fixed);
  } finally {
    producer.close();
    consumer.close();
  }
});

test("high-level socket varlen listen/connect roundtrip", async () => {
  const listener = xproc.socket.listenVarlen({
    host: "127.0.0.1",
    port: 0,
  });
  const consumer = listener.openConsumer();
  const producer = xproc.socket.connectVarlen({
    host: "127.0.0.1",
    port: consumer.socketPort(),
  }).openProducer();

  try {
    producer.sendVarlen("hello-socket");
    const payload = await waitForPoll(consumer);
    assert.equal(Buffer.from(payload).toString("utf8"), "hello-socket");
    assert.equal(consumer.options().backend, xproc.BACKEND.socket);
    assert.equal(consumer.options().type, xproc.CHANNEL_TYPE.varlen);
  } finally {
    producer.close();
    consumer.close();
  }
});
