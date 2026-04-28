const timers = require("node:timers/promises");

const xproc = require("../index.js") as XprocModule;

const delay = timers.setTimeout as (ms: number) => Promise<void>;

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Best-effort cleanup only.
  }
}

async function main(): Promise<void> {
  const shmPath = `/xproc_node_varlen_inprocess_${process.pid}`;
  cleanupShm(shmPath);

  const messages = ["hello", "xproc", "variable-length", "messages"];
  const created = xproc.shm.createVarlenChannel({
    path: shmPath,
    dataCapacity: 32768n,
  });
  const producer = created.openProducer();
  const consumer = xproc.shm.attachVarlenChannel({ path: shmPath }).openConsumer();

  let received = 0;

  try {
    for (const message of messages) {
      producer.sendVarlen(message);
      await delay(8);

      while (true) {
        const payload = consumer.pollCopy();
        if (payload === null) {
          await delay(1);
          continue;
        }

        const text = Buffer.from(payload).toString("utf8");
        console.log(`recv: ${text}`);
        if (text !== messages[received]) {
          throw new Error(`sequence mismatch, expected ${messages[received]} got ${text}`);
        }
        received += 1;
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
