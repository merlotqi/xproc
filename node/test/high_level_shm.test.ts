const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

let shmSequence = 0;

function uniqueShmPath(label: string): string {
  shmSequence += 1;
  return `/xproc_node_high_level_${process.pid}_${Date.now()}_${shmSequence}_${label}`;
}

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Ignore cleanup failures when the segment does not exist anymore.
  }
}

test("high-level shm fixed create/attach infers manifest fields", () => {
  const path = uniqueShmPath("fixed");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({
    path,
    itemSize: 4,
    dataCapacity: 4096n,
    schemaId: 0x1234n,
    creatorTimestampNs: 0x1122334455667788n,
    creatorFlags: 0x8877665544332211n,
  });

  const producer = created.openProducer();
  const consumer = xproc.shm.attachFixedChannel({ path, schemaId: 0x1234n }).openConsumer();
  const observer = xproc.shm.attachFixedChannel({ path, schemaId: 0x1234n }).openObserver();

  try {
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));

    const observed = observer.peekCopy();
    const polled = consumer.pollCopy();
    if (observed === null) {
      throw new Error("expected observer payload");
    }
    if (polled === null) {
      throw new Error("expected consumer payload");
    }
    assert.deepEqual([...Buffer.from(observed)], [1, 2, 3, 4]);
    assert.deepEqual([...Buffer.from(polled)], [1, 2, 3, 4]);

    const attachedConsumerOptions = consumer.options();
    const attachedObserverOptions = observer.options();
    assert.equal(attachedConsumerOptions.itemSize, 4);
    assert.equal(attachedObserverOptions.schemaId, 0x1234n);
    assert.equal(attachedConsumerOptions.creatorTimestampNs, 0x1122334455667788n);
    assert.equal(attachedObserverOptions.creatorFlags, 0x8877665544332211n);
  } finally {
    observer.close();
    consumer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("high-level shm varlen create/attach infers manifest fields", () => {
  const path = uniqueShmPath("varlen");
  cleanupShm(path);

  const created = xproc.shm.createVarlenChannel({
    path,
    dataCapacity: 8192n,
    schemaId: 0x2233n,
  });

  const producer = created.openProducer();
  const consumer = xproc.shm.attachVarlenChannel({ path, schemaId: 0x2233n }).openConsumer();

  try {
    producer.sendVarlen("hello-high-level-varlen");
    const payload = consumer.pollCopy();
    if (payload === null) {
      throw new Error("expected consumer payload");
    }
    assert.equal(Buffer.from(payload).toString("utf8"), "hello-high-level-varlen");
    assert.equal(consumer.options().type, xproc.CHANNEL_TYPE.varlen);
    assert.equal(consumer.options().schemaId, 0x2233n);
  } finally {
    consumer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("high-level shm attach mismatch surfaces layout metadata without itemSize", () => {
  const path = uniqueShmPath("schema_mismatch");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({
    path,
    itemSize: 4,
    dataCapacity: 4096n,
    schemaId: 7n,
  });
  const producer = created.openProducer();

  try {
    assert.throws(
      () => xproc.shm.attachFixedChannel({ path, schemaId: 8n }).openConsumer(),
      (error: unknown) => {
        assert.ok(error instanceof Error);
        if (!(error instanceof Error)) {
          throw new Error("expected Error instance");
        }
        const xprocError = error as Error & { layoutError?: number };
        assert.equal(xprocError.layoutError, xproc.LAYOUT_ERROR.schemaIdMismatch);
        assert.match(xprocError.message, /schema_id mismatch/i);
        return true;
      },
    );
  } finally {
    producer.close();
    cleanupShm(path);
  }
});
