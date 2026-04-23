const childProcess = require("node:child_process");
const timers = require("node:timers/promises");

const xproc = require("../index.js") as XprocModule;

type TelemetryPacket = {
  message: string;
  a: number;
  b: number;
};

type CliOptions = {
  child: boolean;
  shmPath: string | null;
  ticks: number;
  intervalMs: number;
};

const CHILD_FLAG = "--pc-struct-child";
const MESSAGE_BYTES = 256;
const INT32_BYTES = 4;
const PACKET_SIZE = MESSAGE_BYTES + (INT32_BYTES * 2);
const DATA_CAPACITY = 32768n;
const POLL_INTERVAL_MS = 100;

const delay = timers.setTimeout as (ms: number) => Promise<void>;

function parseCli(argv: readonly string[]): CliOptions {
  let child = false;
  let shmPath: string | null = null;
  let ticks = 100;
  let intervalMs = 300;

  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === CHILD_FLAG) {
      child = true;
      shmPath = argv[i + 1] ?? null;
      i += 1;
      continue;
    }
    if (arg.startsWith("--ticks=")) {
      ticks = parseIntegerFlag(arg.slice("--ticks=".length), "ticks");
      continue;
    }
    if (arg.startsWith("--interval-ms=")) {
      intervalMs = parseIntegerFlag(arg.slice("--interval-ms=".length), "interval-ms");
      continue;
    }
  }

  return { child, shmPath, ticks, intervalMs };
}

function parseIntegerFlag(raw: string, name: string): number {
  const value = Number.parseInt(raw, 10);
  if (!Number.isFinite(value) || value < 0) {
    throw new Error(`invalid --${name} value: ${raw}`);
  }
  return value;
}

function createProducerOptions(path: string): TransportOptions {
  return {
    path,
    shmSize: xproc.XPROC_C_INFER_EXISTING_SHM_SIZE,
    channelType: "fixed",
    itemSize: PACKET_SIZE,
    createIfMissing: false,
  };
}

function createConsumerOptions(path: string): TransportOptions {
  return {
    path,
    shmSize: xproc.shmSizeForDataCapacity(DATA_CAPACITY),
    channelType: "fixed",
    itemSize: PACKET_SIZE,
    createIfMissing: true,
  };
}

function encodePacket(packet: TelemetryPacket): Buffer {
  const buffer = Buffer.alloc(PACKET_SIZE);
  const encodedMessage = Buffer.from(packet.message, "utf8");
  encodedMessage.copy(buffer, 0, 0, Math.min(encodedMessage.length, MESSAGE_BYTES - 1));
  buffer.writeInt32LE(packet.a, MESSAGE_BYTES);
  buffer.writeInt32LE(packet.b, MESSAGE_BYTES + INT32_BYTES);
  return buffer;
}

function decodePacket(payload: Uint8Array): TelemetryPacket {
  const buffer = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  let messageEnd = 0;
  while (messageEnd < MESSAGE_BYTES && buffer[messageEnd] !== 0) {
    messageEnd += 1;
  }

  return {
    message: buffer.toString("utf8", 0, messageEnd),
    a: buffer.readInt32LE(MESSAGE_BYTES),
    b: buffer.readInt32LE(MESSAGE_BYTES + INT32_BYTES),
  };
}

function cleanupShm(path: string): void {
  try {
    xproc.shmUnlink(path);
  } catch {
    // Best-effort cleanup only.
  }
}

async function runChildWriter(path: string, ticks: number, intervalMs: number): Promise<void> {
  const producer = new xproc.Producer(createProducerOptions(path));
  try {
    for (let i = 0; i <= ticks; ++i) {
      const packet = encodePacket({
        message: `tick-${i}`,
        a: i,
        b: i * 2,
      });
      producer.sendFixedSized(packet);
      await delay(intervalMs);
    }
  } finally {
    producer.close();
  }
}

function drainConsumer(consumer: Consumer, onPacket: (payload: Uint8Array) => void): boolean {
  let consumedAny = false;
  while (true) {
    const payload = consumer.pollCopy();
    if (payload === null) {
      return consumedAny;
    }
    consumedAny = true;
    if (payload.length !== PACKET_SIZE) {
      continue;
    }
    onPacket(payload);
  }
}

async function runParent(ticks: number, intervalMs: number): Promise<void> {
  const shmPath = `/xproc_example_parent_child_struct_${process.pid}`;
  cleanupShm(shmPath);

  const consumer = new xproc.Consumer(createConsumerOptions(shmPath));
  let childExitCode: number | null = null;
  let lastPacketBytes: Buffer | null = null;

  const child = childProcess.spawn(
    process.execPath,
    ["--experimental-strip-types", process.argv[1], CHILD_FLAG, shmPath, `--ticks=${ticks}`, `--interval-ms=${intervalMs}`],
    {
      stdio: "inherit",
    },
  );

  child.on("exit", (code: number | null) => {
    childExitCode = code ?? 1;
  });

  try {
    while (childExitCode === null) {
      drainConsumer(consumer, (payload) => {
        const currentBytes = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
        if (lastPacketBytes !== null && Buffer.compare(currentBytes, lastPacketBytes) === 0) {
          return;
        }

        const packet = decodePacket(currentBytes);
        console.log(`message=${packet.message}, a=${packet.a}, b=${packet.b}`);
        lastPacketBytes = Buffer.from(currentBytes);
      });
      await delay(POLL_INTERVAL_MS);
    }

    drainConsumer(consumer, (payload) => {
      const currentBytes = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
      if (lastPacketBytes !== null && Buffer.compare(currentBytes, lastPacketBytes) === 0) {
        return;
      }

      const packet = decodePacket(currentBytes);
      console.log(`message=${packet.message}, a=${packet.a}, b=${packet.b}`);
      lastPacketBytes = Buffer.from(currentBytes);
    });
  } finally {
    consumer.close();
    cleanupShm(shmPath);
  }

  if (childExitCode !== 0) {
    throw new Error("child process failed");
  }
  console.log("child exited, parent done");
}

async function main(): Promise<void> {
  const cli = parseCli(process.argv.slice(2));
  if (cli.child) {
    if (cli.shmPath === null) {
      throw new Error(`${CHILD_FLAG} requires a shared-memory path`);
    }
    await runChildWriter(cli.shmPath, cli.ticks, cli.intervalMs);
    return;
  }

  await runParent(cli.ticks, cli.intervalMs);
}

void main().catch((error: unknown) => {
  const message = error instanceof Error ? error.stack ?? error.message : String(error);
  console.error(message);
  process.exit(1);
});
