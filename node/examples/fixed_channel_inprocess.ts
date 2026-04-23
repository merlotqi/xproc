const timers = require("node:timers/promises");

const xproc = require("../index.js") as XprocModule;

const delay = timers.setTimeout as (ms: number) => Promise<void>;
const MESSAGE_COUNT = 10;

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Best-effort cleanup only.
  }
}

async function main(): Promise<void> {
  const shmPath = `/xproc_node_fixed_inprocess_${process.pid}`;
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

  let expected = 1;

  try {
    for (let i = 1; i <= MESSAGE_COUNT; ++i) {
      const payload = Buffer.alloc(4);
      payload.writeInt32LE(i, 0);
      producer.sendFixedSized(payload);
      await delay(10);

      while (true) {
        const message = consumer.pollCopy();
        if (message === null) {
          await delay(1);
          continue;
        }

        const value = Buffer.from(message).readInt32LE(0);
        console.log(`recv: ${value}`);
        if (value !== expected) {
          throw new Error(`sequence mismatch, expected ${expected} got ${value}`);
        }
        expected += 1;
        break;
      }
    }
  } finally {
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
