const childProcess = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const timers = require("node:timers/promises");

type XprocModule = typeof import("../index");
type TransportOptions = import("../index").TransportOptions;
type Consumer = import("../index").Consumer;

const xproc = require("../index.js") as XprocModule;

type TelemetryPacket = {
  message: string;
  a: number;
  b: number;
};

type CliOptions = {
  childBin: string | null;
  ticks: number;
  intervalMs: number;
};

const MESSAGE_BYTES = 256;
const INT32_BYTES = 4;
const PACKET_SIZE = MESSAGE_BYTES + (INT32_BYTES * 2);
const DATA_CAPACITY = 32768n;
const POLL_INTERVAL_MS = 100;
const DEFAULT_CHILD_TARGET = "xproc_node_cpp_child_struct_writer";
const SCHEMA_ID = 0x4e4f44455f435050n; // "NODE_CPP"

const delay = timers.setTimeout as (ms: number) => Promise<void>;

function parseCli(argv: readonly string[]): CliOptions {
  let childBin: string | null = null;
  let ticks = 100;
  let intervalMs = 300;

  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg.startsWith("--child-bin=")) {
      childBin = arg.slice("--child-bin=".length);
      continue;
    }
    if (arg === "--child-bin" && i + 1 < argv.length) {
      childBin = argv[++i] ?? null;
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

  return { childBin, ticks, intervalMs };
}

function parseIntegerFlag(raw: string, name: string): number {
  const value = Number.parseInt(raw, 10);
  if (!Number.isFinite(value) || value < 0) {
    throw new Error(`invalid --${name} value: ${raw}`);
  }
  return value;
}

function findChildBinary(explicitPath: string | null): string {
  const candidates = explicitPath !== null
    ? [explicitPath]
    : [
      path.resolve(__dirname, "..", "..", "build", "examples", DEFAULT_CHILD_TARGET),
      path.resolve(__dirname, "..", "..", "build", "examples", `${DEFAULT_CHILD_TARGET}.exe`),
    ];

  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }

  throw new Error(`unable to locate C++ child binary. Tried: ${candidates.join(", ")}`);
}

function createConsumerOptions(shmPath: string): TransportOptions {
  return {
    path: shmPath,
    shmSize: xproc.shmSizeForDataCapacity(DATA_CAPACITY),
    channelType: "fixed",
    itemSize: PACKET_SIZE,
    schemaId: SCHEMA_ID,
    createIfMissing: true,
  };
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

function cleanupShm(pathValue: string): void {
  try {
    xproc.shmUnlink(pathValue);
  } catch {
    // Best-effort cleanup only.
  }
}

function drainConsumer(consumer: Consumer, onPacket: (payload: Uint8Array) => void): void {
  while (true) {
    const payload = consumer.pollCopy();
    if (payload === null) {
      return;
    }
    if (payload.length !== PACKET_SIZE) {
      continue;
    }
    onPacket(payload);
  }
}

async function main(): Promise<void> {
  const cli = parseCli(process.argv.slice(2));
  const childBin = findChildBinary(cli.childBin);
  const shmPath = `/xproc_node_parent_cpp_child_struct_${process.pid}`;

  cleanupShm(shmPath);

  const consumer = new xproc.Consumer(createConsumerOptions(shmPath));
  let childExitCode: number | null = null;
  let lastPacketBytes: Buffer | null = null;

  const child = childProcess.spawn(
    childBin,
    ["--shm-path", shmPath, "--ticks", String(cli.ticks), "--interval-ms", String(cli.intervalMs)],
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
    throw new Error("C++ child process failed");
  }

  console.log("child exited, parent done");
}

void main().catch((error: unknown) => {
  const message = error instanceof Error ? error.stack ?? error.message : String(error);
  console.error(message);
  process.exit(1);
});
