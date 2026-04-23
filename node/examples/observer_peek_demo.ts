const timers = require("node:timers/promises");

type XprocModule = typeof import("../index");
type TransportOptions = import("../index").TransportOptions;
type Observer = import("../index").Observer;
type Consumer = import("../index").Consumer;

const xproc = require("../index.js") as XprocModule;

const delay = timers.setTimeout as (ms: number) => Promise<void>;

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Best-effort cleanup only.
  }
}

async function waitForPeek(observer: Observer): Promise<Uint8Array> {
  while (true) {
    const payload = observer.peekCopy();
    if (payload !== null) {
      return payload;
    }
    await delay(1);
  }
}

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
  const shmPath = `/xproc_node_observer_demo_${process.pid}`;
  cleanupShm(shmPath);

  const createOptions: TransportOptions = {
    path: shmPath,
    shmSize: xproc.shmSizeForDataCapacity(16384n),
    channelType: "fixed",
    itemSize: 4,
    createIfMissing: true,
  };

  const attachOptions: TransportOptions = {
    ...createOptions,
    shmSize: xproc.XPROC_C_INFER_EXISTING_SHM_SIZE,
    createIfMissing: false,
  };

  const producer = new xproc.Producer(createOptions);
  const consumer = new xproc.Consumer(attachOptions);
  const observer = new xproc.Observer(attachOptions);

  try {
    const payload = Buffer.alloc(4);
    payload.writeInt32LE(0x42, 0);
    producer.sendFixedSized(payload);

    const peeked = await waitForPeek(observer);
    console.log(`observer sees: ${Buffer.from(peeked).readInt32LE(0)}`);

    const consumed = await waitForPoll(consumer);
    console.log(`consumer got: ${Buffer.from(consumed).readInt32LE(0)}, len=${consumed.length}`);

    const snapshot = observer.snapshot();
    console.log(
      `snapshot writePos=${snapshot.writePos} readPos=${snapshot.readPos} attachCount=${snapshot.attachCount}`,
    );
  } finally {
    observer.close();
    consumer.close();
    producer.close();
    cleanupShm(shmPath);
  }
}

void main().catch((error: unknown) => {
  const message = error instanceof Error ? error.stack ?? error.message : String(error);
  console.error(message);
  process.exit(1);
});
