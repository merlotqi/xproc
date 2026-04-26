/**
 * Node.js bindings for xproc.
 *
 * The recommended entrypoints are:
 * - `xproc.shm.*` for shared-memory channels
 * - `xproc.socket.*` for TCP loopback / network transports
 *
 * The lower-level `Producer` / `Consumer` / `Observer` constructors are still
 * available when you need to pass raw transport options yourself.
 */
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

  /**
   * Low-level transport configuration for the raw endpoint constructors.
   *
   * Most consumers should prefer the higher-level `xproc.shm.*` and
   * `xproc.socket.*` helpers, which derive the transport options for the common
   * cases.
   */
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

  /** Normalized transport options returned by `options()` on opened endpoints. */
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

  /** Options for creating a fixed-size shared-memory channel. */
  interface ShmFixedCreateOptions {
    /** Shared-memory object path, for example `/demo-fixed`. */
    path: string;
    /** Number of bytes in each frame. */
    itemSize: IntegerLike;
    /** Requested ring-buffer payload capacity, excluding xproc metadata. */
    dataCapacity: IntegerLike;
    /** Optional payload alignment override. */
    dataAlign?: IntegerLike;
    /** Optional manifest schema identifier used for attach-time validation. */
    schemaId?: IntegerLike;
    /** Optional creator timestamp recorded in the manifest. */
    creatorTimestampNs?: IntegerLike;
    /** Optional creator flags recorded in the manifest. */
    creatorFlags?: IntegerLike;
    /** Optional Windows object namespace. */
    win32ObjectNamespace?: string | null;
  }

  /** Options for creating a variable-length shared-memory channel. */
  interface ShmVarlenCreateOptions {
    /** Shared-memory object path, for example `/demo-varlen`. */
    path: string;
    /** Requested ring-buffer payload capacity, excluding xproc metadata. */
    dataCapacity: IntegerLike;
    /** Optional payload alignment override. */
    dataAlign?: IntegerLike;
    /** Optional manifest schema identifier used for attach-time validation. */
    schemaId?: IntegerLike;
    /** Optional creator timestamp recorded in the manifest. */
    creatorTimestampNs?: IntegerLike;
    /** Optional creator flags recorded in the manifest. */
    creatorFlags?: IntegerLike;
    /** Optional Windows object namespace. */
    win32ObjectNamespace?: string | null;
  }

  /** Options for attaching to an existing shared-memory channel. */
  interface ShmAttachOptions {
    /** Shared-memory object path that was used by the creator. */
    path: string;
    /** Optional schema check performed against the manifest-backed channel. */
    schemaId?: IntegerLike;
    /** Optional Windows object namespace. */
    win32ObjectNamespace?: string | null;
  }

  /** Factory object returned by high-level shared-memory create and attach APIs. */
  interface ShmChannelEndpoints {
    /** Returns the normalized transport options that will be used for new endpoints. */
    options(): ResolvedTransportOptions;
    /** Opens a producer for the shared-memory channel. */
    openProducer(): Producer;
    /** Opens a consumer for the shared-memory channel. */
    openConsumer(): Consumer;
    /** Opens a read-only observer for the shared-memory channel. */
    openObserver(): Observer;
  }

  /**
   * High-level shared-memory API.
   *
   * @example
   * ```ts
   * const created = xproc.shm.createFixedChannel({
   *   path: "/demo-fixed",
   *   itemSize: 4,
   *   dataCapacity: 16384n,
   * });
   *
   * const producer = created.openProducer();
   * const consumer = xproc.shm.attachFixedChannel({ path: "/demo-fixed" }).openConsumer();
   * ```
   */
  interface ShmNamespace {
    /** Creates a manifest-backed fixed-size shared-memory channel. */
    createFixedChannel(options: ShmFixedCreateOptions): ShmChannelEndpoints;
    /** Attaches to an existing fixed-size shared-memory channel without repeating `itemSize`. */
    attachFixedChannel(options: ShmAttachOptions): ShmChannelEndpoints;
    /** Creates a manifest-backed variable-length shared-memory channel. */
    createVarlenChannel(options: ShmVarlenCreateOptions): ShmChannelEndpoints;
    /** Attaches to an existing variable-length shared-memory channel. */
    attachVarlenChannel(options: ShmAttachOptions): ShmChannelEndpoints;
  }

  /** Options for listening for fixed-size socket traffic. */
  interface SocketListenFixedOptions {
    /** Host or interface to bind. Defaults to the platform socket default if omitted. */
    host?: string | null;
    /** Port to bind. Use `0` to let the OS choose an ephemeral port. */
    port: IntegerLike;
    /** Number of bytes in each fixed-size frame. */
    itemSize: IntegerLike;
  }

  /** Options for connecting to a fixed-size socket listener. */
  interface SocketConnectFixedOptions {
    /** Host name or IP address of the listener. */
    host: string;
    /** Listener port. */
    port: IntegerLike;
    /** Number of bytes in each fixed-size frame. */
    itemSize: IntegerLike;
    /** Optional number of connection retries before failing. */
    connectRetries?: number;
    /** Optional delay between connection retries in milliseconds. */
    connectRetryMs?: number;
  }

  /** Options for listening for variable-length socket traffic. */
  interface SocketListenVarlenOptions {
    /** Host or interface to bind. Defaults to the platform socket default if omitted. */
    host?: string | null;
    /** Port to bind. Use `0` to let the OS choose an ephemeral port. */
    port: IntegerLike;
  }

  /** Options for connecting to a variable-length socket listener. */
  interface SocketConnectVarlenOptions {
    /** Host name or IP address of the listener. */
    host: string;
    /** Listener port. */
    port: IntegerLike;
    /** Optional number of connection retries before failing. */
    connectRetries?: number;
    /** Optional delay between connection retries in milliseconds. */
    connectRetryMs?: number;
  }

  /** Listener handle returned by `xproc.socket.listen*()`. */
  interface SocketListener {
    /** Returns the normalized transport options that will be used for new endpoints. */
    options(): ResolvedTransportOptions;
    /** Opens the consumer side of the listener. */
    openConsumer(): Consumer;
  }

  /** Connector handle returned by `xproc.socket.connect*()`. */
  interface SocketConnector {
    /** Returns the normalized transport options that will be used for new endpoints. */
    options(): ResolvedTransportOptions;
    /** Opens the producer side of the connection. */
    openProducer(): Producer;
  }

  /**
   * High-level socket API.
   *
   * @example
   * ```ts
   * const listener = xproc.socket.listenVarlen({ host: "127.0.0.1", port: 0 });
   * const consumer = listener.openConsumer();
   * const producer = xproc.socket.connectVarlen({
   *   host: "127.0.0.1",
   *   port: consumer.socketPort(),
   * }).openProducer();
   * ```
   */
  interface SocketNamespace {
    /** Starts a fixed-size socket listener and returns a handle that can open a consumer. */
    listenFixed(options: SocketListenFixedOptions): SocketListener;
    /** Creates a connector for a fixed-size socket producer. */
    connectFixed(options: SocketConnectFixedOptions): SocketConnector;
    /** Starts a variable-length socket listener and returns a handle that can open a consumer. */
    listenVarlen(options: SocketListenVarlenOptions): SocketListener;
    /** Creates a connector for a variable-length socket producer. */
    connectVarlen(options: SocketConnectVarlenOptions): SocketConnector;
  }

  /** Snapshot of channel positions and metadata returned by `Observer.snapshot()`. */
  interface Snapshot {
    writePos: bigint;
    readPos: bigint;
    commitSeq: number;
    readWakeSeq: number;
    attachCount: number;
    producerPid: number;
  }

  /** Error shape used by xproc layout and transport failures. */
  interface XprocError extends Error {
    /** High-level status enum value such as `STATUS.layoutError`. */
    status: number;
    /** Human-readable name for `status`. */
    statusCode: string;
    /** Layout-specific error code when `status === STATUS.layoutError`. */
    layoutError: number;
    /** Human-readable name for `layoutError`. */
    layoutErrorCode: string;
  }

  /** Producer endpoint for fixed-size or variable-length messages. */
  class Producer {
    /**
     * Opens a raw producer from low-level transport options.
     *
     * Prefer `xproc.shm.create*().openProducer()` and
     * `xproc.socket.connect*().openProducer()` unless you need fine-grained
     * control over the transport fields.
     */
    constructor(options?: TransportOptions);
    /** Closes the endpoint and releases the native handle. */
    close(): void;
    /** Returns the resolved transport options used by the native endpoint. */
    options(): ResolvedTransportOptions;
    /** Sends a fixed-size payload without requiring an exact item-size match. */
    sendFixedBytes(data: ByteSource): void;
    /** Sends one fixed-size frame and validates that `data.length === itemSize`. */
    sendFixedSized(data: ByteSource): void;
    /** Sends one variable-length payload. */
    sendVarlen(data: ByteSource): void;
    /** Returns the bound or connected socket port for socket-backed producers. */
    socketPort(): number;
  }

  /** Consumer endpoint for polling or blocking reads. */
  class Consumer {
    /** Opens a raw consumer from low-level transport options. */
    constructor(options?: TransportOptions);
    /** Closes the endpoint and releases the native handle. */
    close(): void;
    /** Returns the resolved transport options used by the native endpoint. */
    options(): ResolvedTransportOptions;
    /** Returns the pending payload length, or `0` when no message is available. */
    pendingLen(): number;
    /** Polls for a payload and returns `null` when the channel is currently empty. */
    pollCopy(): Uint8Array | null;
    /** Blocks until data is available or the underlying transport wakes the consumer. */
    wait(): void;
    /** Returns the bound or connected socket port for socket-backed consumers. */
    socketPort(): number;
  }

  /** Read-only observer endpoint for peeking at the latest committed payload. */
  class Observer {
    /** Opens a raw observer from low-level transport options. */
    constructor(options?: TransportOptions);
    /** Closes the endpoint and releases the native handle. */
    close(): void;
    /** Returns the resolved transport options used by the native endpoint. */
    options(): ResolvedTransportOptions;
    /** Returns channel counters and producer metadata for diagnostics. */
    snapshot(): Snapshot;
    /** Returns the latest visible payload without consuming it, or `null` when empty. */
    peekCopy(): Uint8Array | null;
  }

  /** Converts requested payload capacity into the shared-memory size expected by xproc. */
  function shmSizeForDataCapacity(dataCapacity: IntegerLike): bigint;
  /** Converts a shared-memory size back into usable payload capacity. */
  function shmDataCapacityForSize(shmSize: IntegerLike): bigint;
  /** Returns the symbolic name for a status code. */
  function statusString(status: StatusCode | number): string;
  /** Returns the symbolic name for a layout error code. */
  function layoutErrorString(error: LayoutError | number): string;
  /** Returns the linked xproc library version string. */
  function versionString(): string;
  /** Returns the current process ID as observed by the native binding. */
  function currentProcessId(): number;
  /** Validates raw transport options for the requested endpoint kind. Throws on invalid input. */
  function validateOptionsFor(kind: EndpointKindInput, options: TransportOptions): true;
  /** Removes a shared-memory object by path. Useful for test cleanup and local demos. */
  function shmUnlink(path: string): void;

  /** High-level shared-memory helpers for the common create and attach flows. */
  const shm: ShmNamespace;
  /** High-level socket helpers for listen and connect flows. */
  const socket: SocketNamespace;

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
