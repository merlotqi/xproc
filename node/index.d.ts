declare namespace xproc {
  type IntegerLike = number | bigint;
  type BooleanLike = boolean | 0 | 1;
  type ByteSource = Uint8Array | ArrayBuffer | ArrayBufferView | string | readonly number[];

  type StatusCode = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8;
  type EndpointKind = 0 | 1 | 2;
  type Backend = 0 | 1;
  type ChannelType = 0 | 1;
  type LayoutError = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10;

  type BackendName = "shared_memory" | "shared-memory" | "sharedmemory" | "shm" | "socket" | "tcp";
  type ChannelTypeName = "fixed" | "varlen" | "variable" | "variable_length" | "variable-length";
  type EndpointKindName = "producer" | "consumer" | "observer";

  type BackendInput = Backend | BackendName;
  type ChannelTypeInput = ChannelType | ChannelTypeName;
  type EndpointKindInput = EndpointKind | EndpointKindName;

  interface TransportOptions {
    backend?: BackendInput;
    path?: string | null;
    shmSize?: IntegerLike;
    itemSize?: IntegerLike;
    dataAlign?: IntegerLike;
    schemaId?: IntegerLike;
    creatorTimestampNs?: IntegerLike;
    creatorFlags?: IntegerLike;
    createIfMissing?: BooleanLike;
    channelType?: ChannelTypeInput;
    type?: ChannelTypeInput;
    win32ObjectNamespace?: string | null;
    socketHost?: string | null;
    socketPort?: IntegerLike;
    socketListen?: BooleanLike;
    socketConnectRetries?: number;
    socketConnectRetryMs?: number;
  }

  interface ResolvedTransportOptions {
    backend: Backend;
    path?: string;
    shmSize: bigint;
    itemSize: number;
    dataAlign: number;
    schemaId: bigint;
    creatorTimestampNs: bigint;
    creatorFlags: bigint;
    createIfMissing: boolean;
    channelType: ChannelType;
    type: ChannelType;
    win32ObjectNamespace?: string;
    socketHost?: string;
    socketPort: number;
    socketListen: boolean;
    socketConnectRetries: number;
    socketConnectRetryMs: number;
  }

  interface Snapshot {
    writePos: bigint;
    readPos: bigint;
    commitSeq: number;
    readWakeSeq: number;
    attachCount: number;
    producerPid: number;
  }

  interface XprocError extends Error {
    status: number;
    statusCode: string;
    layoutError: number;
    layoutErrorCode: string;
  }

  class Producer {
    constructor(options?: TransportOptions);
    close(): void;
    options(): ResolvedTransportOptions;
    sendFixedBytes(data: ByteSource): void;
    sendFixedSized(data: ByteSource): void;
    sendVarlen(data: ByteSource): void;
    socketPort(): number;
  }

  class Consumer {
    constructor(options?: TransportOptions);
    close(): void;
    options(): ResolvedTransportOptions;
    pendingLen(): number;
    pollCopy(): Uint8Array | null;
    wait(): void;
    socketPort(): number;
  }

  class Observer {
    constructor(options?: TransportOptions);
    close(): void;
    options(): ResolvedTransportOptions;
    snapshot(): Snapshot;
    peekCopy(): Uint8Array | null;
  }

  function shmSizeForDataCapacity(dataCapacity: IntegerLike): bigint;
  function shmDataCapacityForSize(shmSize: IntegerLike): bigint;
  function statusString(status: StatusCode | number): string;
  function layoutErrorString(error: LayoutError | number): string;
  function versionString(): string;
  function currentProcessId(): number;
  function validateOptionsFor(kind: EndpointKindInput, options: TransportOptions): true;
  function shmUnlink(path: string): void;

  const XPROC_C_STATUS_OK: 0;
  const XPROC_C_STATUS_AGAIN: 1;
  const XPROC_C_STATUS_BUFFER_TOO_SMALL: 2;
  const XPROC_C_STATUS_INVALID_ARGUMENT: 3;
  const XPROC_C_STATUS_LOGIC_ERROR: 4;
  const XPROC_C_STATUS_LAYOUT_ERROR: 5;
  const XPROC_C_STATUS_RUNTIME_ERROR: 6;
  const XPROC_C_STATUS_NO_MEMORY: 7;
  const XPROC_C_STATUS_INTERNAL_ERROR: 8;

  const XPROC_C_ENDPOINT_PRODUCER: 0;
  const XPROC_C_ENDPOINT_CONSUMER: 1;
  const XPROC_C_ENDPOINT_OBSERVER: 2;

  const XPROC_C_BACKEND_SHARED_MEMORY: 0;
  const XPROC_C_BACKEND_SOCKET: 1;

  const XPROC_C_CHANNEL_FIXED: 0;
  const XPROC_C_CHANNEL_VARLEN: 1;
  const XPROC_C_INFER_EXISTING_SHM_SIZE: 0;

  const XPROC_C_LAYOUT_ERROR_NONE: 0;
  const XPROC_C_LAYOUT_ERROR_NOT_ATTACHED: 1;
  const XPROC_C_LAYOUT_ERROR_BAD_MAGIC: 2;
  const XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT: 3;
  const XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH: 4;
  const XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH: 5;
  const XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH: 6;
  const XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH: 7;
  const XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH: 8;
  const XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID: 9;
  const XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT: 10;

  const STATUS: Readonly<{
    ok: typeof XPROC_C_STATUS_OK;
    again: typeof XPROC_C_STATUS_AGAIN;
    bufferTooSmall: typeof XPROC_C_STATUS_BUFFER_TOO_SMALL;
    invalidArgument: typeof XPROC_C_STATUS_INVALID_ARGUMENT;
    logicError: typeof XPROC_C_STATUS_LOGIC_ERROR;
    layoutError: typeof XPROC_C_STATUS_LAYOUT_ERROR;
    runtimeError: typeof XPROC_C_STATUS_RUNTIME_ERROR;
    noMemory: typeof XPROC_C_STATUS_NO_MEMORY;
    internalError: typeof XPROC_C_STATUS_INTERNAL_ERROR;
  }>;

  const ENDPOINT_KIND: Readonly<{
    producer: typeof XPROC_C_ENDPOINT_PRODUCER;
    consumer: typeof XPROC_C_ENDPOINT_CONSUMER;
    observer: typeof XPROC_C_ENDPOINT_OBSERVER;
  }>;

  const BACKEND: Readonly<{
    sharedMemory: typeof XPROC_C_BACKEND_SHARED_MEMORY;
    socket: typeof XPROC_C_BACKEND_SOCKET;
  }>;

  const CHANNEL_TYPE: Readonly<{
    fixed: typeof XPROC_C_CHANNEL_FIXED;
    varlen: typeof XPROC_C_CHANNEL_VARLEN;
  }>;

  const LAYOUT_ERROR: Readonly<{
    none: typeof XPROC_C_LAYOUT_ERROR_NONE;
    notAttached: typeof XPROC_C_LAYOUT_ERROR_NOT_ATTACHED;
    badMagic: typeof XPROC_C_LAYOUT_ERROR_BAD_MAGIC;
    notReadyTimeout: typeof XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT;
    versionMismatch: typeof XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH;
    headerSizeMismatch: typeof XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH;
    layoutTypeMismatch: typeof XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH;
    fixedItemSizeMismatch: typeof XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH;
    schemaIdMismatch: typeof XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH;
    alignmentInvalid: typeof XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID;
    capacityInsufficient: typeof XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT;
  }>;
}

export = xproc;
