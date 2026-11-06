const assert = require("node:assert/strict") as typeof import("node:assert/strict");
const test = require("node:test") as typeof import("node:test");

const xproc = require("../index.js") as XprocModule;

test("layout error enum includes fixedItemSizeIncompatible", () => {
  assert.equal(xproc.LAYOUT_ERROR.fixedItemSizeIncompatible, 11);
  assert.ok(xproc.layoutErrorString(11).length > 0);
});

let shmSequence = 0;

function uniqueShmPath(label: string): string {
  shmSequence += 1;
  return `/xproc_node_meta_${process.pid}_${Date.now()}_${shmSequence}_${label}`;
}

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Ignore cleanup failures when the segment does not exist anymore.
  }
}

test("varlen send with meta roundtrips through peekCopyWithMeta", () => {
  const path = uniqueShmPath("varlen_meta");
  cleanupShm(path);

  const created = xproc.shm.createVarlenChannel({ path, dataCapacity: 32768n });
  const producer = created.openProducer();
  const observer = xproc.shm.attachVarlenChannel({ path }).openObserver();

  try {
    producer.sendVarlen("hello", {
      userData: 0x1122334455667788n,
      timestampNs: 123456789n,
      schemaId: 0xabcd,
      flags: 0x7,
    });

    const peeked = observer.peekCopyWithMeta();
    if (peeked === null) {
      throw new Error("expected peeked message");
    }
    assert.equal(Buffer.from(peeked.payload).toString(), "hello");
    assert.equal(peeked.meta.userData, 0x1122334455667788n);
    assert.equal(peeked.meta.timestampNs, 123456789n);
    assert.equal(peeked.meta.schemaId, 0xabcd);
    assert.equal(peeked.meta.flags, 0x7);
  } finally {
    observer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("fixed-channel send with meta transports zero meta", () => {
  const path = uniqueShmPath("fixed_meta");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity: 4096n });
  const producer = created.openProducer();
  const consumer = created.openConsumer();
  const observer = xproc.shm.attachFixedChannel({ path }).openObserver();

  try {
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]), { userData: 99n, flags: 3 });

    const withMeta = observer.peekCopyWithMeta();
    if (withMeta === null) {
      throw new Error("expected peeked message");
    }
    assert.deepEqual([...Buffer.from(withMeta.payload)], [1, 2, 3, 4]);
    assert.equal(withMeta.meta.userData, 0n);
    assert.equal(withMeta.meta.timestampNs, 0n);
    assert.equal(withMeta.meta.schemaId, 0);
    assert.equal(withMeta.meta.flags, 0);

    // Drain message 1 so the observer's next peek observes message 2: peek
    // reads the oldest-unconsumed slot at the shared read position and never
    // advances it — only a consumer does.
    const drained = consumer.pollCopy();
    if (drained === null) {
      throw new Error("expected consumer payload");
    }
    assert.deepEqual([...Buffer.from(drained)], [1, 2, 3, 4]);

    producer.sendFixedSized(Buffer.from([5, 6, 7, 8]));

    const withoutMeta = observer.peekCopyWithMeta();
    if (withoutMeta === null) {
      throw new Error("expected peeked message");
    }
    assert.deepEqual([...Buffer.from(withoutMeta.payload)], [5, 6, 7, 8]);
    assert.equal(withoutMeta.meta.userData, 0n);
    assert.equal(withoutMeta.meta.flags, 0);
  } finally {
    observer.close();
    consumer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("observer diagnostics getters report sane values", () => {
  const path = uniqueShmPath("diagnostics");
  cleanupShm(path);

  const dataCapacity = 4096n;
  const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity });
  const producer = created.openProducer();
  const observer = xproc.shm.attachFixedChannel({ path }).openObserver();

  try {
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));

    const ratio = observer.occupancyRatio();
    assert.ok(ratio >= 0 && ratio <= 1);

    const occupied = observer.occupancyBytes();
    assert.equal(typeof occupied, "bigint");
    assert.ok(occupied > 0n);
    assert.ok(occupied <= dataCapacity);

    const available = observer.availableBytes();
    assert.equal(typeof available, "bigint");
    assert.ok(available >= 0n);
    assert.ok(available <= dataCapacity);

    const lag = observer.consumerLagBytes();
    assert.equal(typeof lag, "bigint");
    assert.ok(lag >= 0n);
  } finally {
    observer.close();
    producer.close();
    cleanupShm(path);
  }
});

test("diagnostics tracker detects producer activity and guards observer close", () => {
  const path = uniqueShmPath("tracker");
  cleanupShm(path);

  const created = xproc.shm.createFixedChannel({ path, itemSize: 4, dataCapacity: 4096n });
  const producer = created.openProducer();
  const observer = xproc.shm.attachFixedChannel({ path }).openObserver();
  let tracker: import("../index").DiagnosticsTracker | undefined;

  try {
    tracker = observer.createDiagnosticsTracker();
    producer.sendFixedSized(Buffer.from([1, 2, 3, 4]));
    tracker.update();

    assert.equal(tracker.producerAlive(), true);
    assert.ok(tracker.idleMs() >= 0n);

    // The observer must outlive the tracker: closing it early is a TypeError.
    assert.throws(
      () => observer.close(),
      (err: unknown) => err instanceof TypeError && String(err).includes("diagnostics tracker"),
    );

    // The observer is still usable while the tracker is open.
    observer.snapshot();

    tracker.close();
    observer.close(); // now succeeds
  } finally {
    tracker?.close(); // idempotent — releases the observer close guard if the body failed early
    observer.close(); // no-op when already closed above
    producer.close();
    cleanupShm(path);
  }
});
