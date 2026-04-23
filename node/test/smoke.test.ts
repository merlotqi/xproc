const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

let shmSequence = 0;

function uniqueShmPath(label: string): string {
  shmSequence += 1;
  return `/xproc_node_test_${process.pid}_${Date.now()}_${shmSequence}_${label}`;
}

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Ignore cleanup failures when the segment does not exist anymore.
  }
}

test("node ts smoke: fixed channel roundtrip with observer and inferred attach size", () => {
  const path = uniqueShmPath("roundtrip");
  cleanupShm(path);

  const createOptions: TransportOptions = {
    path,
    shmSize: xproc.shmSizeForDataCapacity(4096n),
    channelType: xproc.CHANNEL_TYPE.fixed,
    itemSize: 4,
    schemaId: 0x1234n,
  };

  const attachOptions: TransportOptions = {
    ...createOptions,
    shmSize: xproc.XPROC_C_INFER_EXISTING_SHM_SIZE,
    createIfMissing: false,
  };

  let producer: InstanceType<typeof xproc.Producer> | null = null;
  let consumer: InstanceType<typeof xproc.Consumer> | null = null;
  let observer: InstanceType<typeof xproc.Observer> | null = null;

  try {
    assert.equal(xproc.validateOptionsFor(xproc.ENDPOINT_KIND.producer, createOptions), true);

    producer = new xproc.Producer(createOptions);
    consumer = new xproc.Consumer(attachOptions);
    observer = new xproc.Observer(attachOptions);

    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));

    const peeked = observer.peekCopy();
    assert.ok(Buffer.isBuffer(peeked));
    if (peeked === null) {
      throw new Error("observer.peekCopy() returned null unexpectedly");
    }
    assert.deepEqual([...peeked], [1, 2, 3, 4]);

    const polled = consumer.pollCopy();
    assert.ok(Buffer.isBuffer(polled));
    if (polled === null) {
      throw new Error("consumer.pollCopy() returned null unexpectedly");
    }
    assert.deepEqual([...polled], [1, 2, 3, 4]);

    assert.equal(consumer.pollCopy(), null);

    const producerOptions = producer.options() as { schemaId?: bigint; type?: number };
    const consumerOptions = consumer.options() as { schemaId?: bigint; shmSize?: bigint | number };
    assert.equal(producerOptions.schemaId, 0x1234n);
    assert.equal(producerOptions.type, xproc.CHANNEL_TYPE.fixed);
    assert.equal(consumerOptions.schemaId, 0x1234n);
    assert.equal(consumerOptions.shmSize, BigInt(xproc.XPROC_C_INFER_EXISTING_SHM_SIZE));
  } finally {
    observer?.close();
    consumer?.close();
    producer?.close();
    cleanupShm(path);
  }
});

test("node ts smoke: schema mismatch surfaces layout metadata on the thrown error", () => {
  const path = uniqueShmPath("schema_mismatch");
  cleanupShm(path);

  const createOptions: TransportOptions = {
    path,
    shmSize: xproc.shmSizeForDataCapacity(4096n),
    channelType: "fixed",
    itemSize: 4,
    schemaId: 7n,
  };

  let producer: InstanceType<typeof xproc.Producer> | null = null;

  try {
    producer = new xproc.Producer(createOptions);

    assert.throws(
      () =>
        new xproc.Consumer({
          path,
          shmSize: xproc.XPROC_C_INFER_EXISTING_SHM_SIZE,
          createIfMissing: false,
          channelType: "fixed",
          itemSize: 4,
          schemaId: 8n,
        }),
      (error: unknown) => {
        assert.ok(error instanceof Error);
        if (!(error instanceof Error)) {
          throw new Error("expected Error instance");
        }
        const xprocError = error as Error & { layoutError?: number };
        const layoutError = xprocError.layoutError;
        assert.equal(layoutError, xproc.LAYOUT_ERROR.schemaIdMismatch);
        assert.match(xprocError.message, /schema_id mismatch/i);
        return true;
      },
    );
  } finally {
    producer?.close();
    cleanupShm(path);
  }
});
