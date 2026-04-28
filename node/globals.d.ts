declare function require(id: string): any;
declare const __dirname: string;

declare const process: {
  pid: number;
  argv: string[];
  execPath: string;
  exit(code?: number): never;
};

declare class Buffer extends Uint8Array {
  static alloc(size: number): Buffer;
  static from(data: string, encoding?: string): Buffer;
  static from(data: ArrayBuffer | ArrayBufferView | readonly number[]): Buffer;
  static compare(left: Uint8Array, right: Uint8Array): number;
  static isBuffer(value: unknown): value is Buffer;
  copy(target: Uint8Array, targetStart?: number, sourceStart?: number, sourceEnd?: number): number;
  toString(encoding?: string, start?: number, end?: number): string;
  writeInt32LE(value: number, offset?: number): number;
  readInt32LE(offset?: number): number;
}

declare const console: {
  log(...args: unknown[]): void;
  error(...args: unknown[]): void;
};

declare module "node:assert/strict" {
  interface Assert {
    equal(actual: unknown, expected: unknown, message?: string): void;
    ok(value: unknown, message?: string): void;
    deepEqual(actual: unknown, expected: unknown, message?: string): void;
    match(value: string, regexp: RegExp, message?: string): void;
    throws(block: () => unknown, error?: RegExp | ((error: unknown) => boolean), message?: string): void;
  }

  const assert: Assert;
  export = assert;
}

declare module "node:test" {
  type TestCallback = () => void | Promise<void>;
  type TestFunction = (name: string, fn: TestCallback) => void;

  const test: TestFunction;
  export = test;
}

declare module "node:child_process" {
  interface SpawnOptions {
    stdio?: string;
  }

  interface ChildProcess {
    on(event: "exit", listener: (code: number | null) => void): this;
  }

  function spawn(command: string, args?: readonly string[], options?: SpawnOptions): ChildProcess;

  export { spawn };
}

declare module "node:fs" {
  function existsSync(path: string): boolean;

  export { existsSync };
}

declare module "node:path" {
  function resolve(...paths: string[]): string;

  export { resolve };
}

declare module "node:timers/promises" {
  function setTimeout(delay: number): Promise<void>;

  export { setTimeout };
}
