declare function require(id: string): any;

declare const process: {
  pid: number;
};

declare class Buffer extends Uint8Array {
  static from(data: string, encoding?: string): Buffer;
  static from(data: ArrayBuffer | ArrayBufferView | readonly number[]): Buffer;
  static isBuffer(value: unknown): value is Buffer;
}

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
