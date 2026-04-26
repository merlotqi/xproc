"use strict";

const loadNative = require("./lib/load-native");

const nativeModule = loadNative(__dirname);
const { _readExistingShmOptions, ...native } = nativeModule;

const STATUS = Object.freeze({
  ok: native.XPROC_C_STATUS_OK,
  again: native.XPROC_C_STATUS_AGAIN,
  bufferTooSmall: native.XPROC_C_STATUS_BUFFER_TOO_SMALL,
  invalidArgument: native.XPROC_C_STATUS_INVALID_ARGUMENT,
  logicError: native.XPROC_C_STATUS_LOGIC_ERROR,
  layoutError: native.XPROC_C_STATUS_LAYOUT_ERROR,
  runtimeError: native.XPROC_C_STATUS_RUNTIME_ERROR,
  noMemory: native.XPROC_C_STATUS_NO_MEMORY,
  internalError: native.XPROC_C_STATUS_INTERNAL_ERROR,
});

const ENDPOINT_KIND = Object.freeze({
  producer: native.XPROC_C_ENDPOINT_PRODUCER,
  consumer: native.XPROC_C_ENDPOINT_CONSUMER,
  observer: native.XPROC_C_ENDPOINT_OBSERVER,
});

const BACKEND = Object.freeze({
  sharedMemory: native.XPROC_C_BACKEND_SHARED_MEMORY,
  socket: native.XPROC_C_BACKEND_SOCKET,
});

const CHANNEL_TYPE = Object.freeze({
  fixed: native.XPROC_C_CHANNEL_FIXED,
  varlen: native.XPROC_C_CHANNEL_VARLEN,
});

const LAYOUT_ERROR = Object.freeze({
  none: native.XPROC_C_LAYOUT_ERROR_NONE,
  notAttached: native.XPROC_C_LAYOUT_ERROR_NOT_ATTACHED,
  badMagic: native.XPROC_C_LAYOUT_ERROR_BAD_MAGIC,
  notReadyTimeout: native.XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT,
  versionMismatch: native.XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH,
  headerSizeMismatch: native.XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH,
  layoutTypeMismatch: native.XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH,
  fixedItemSizeMismatch: native.XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH,
  schemaIdMismatch: native.XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH,
  alignmentInvalid: native.XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID,
  capacityInsufficient: native.XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT,
});

function normalizeBackend(value) {
  if (value === undefined) {
    return BACKEND.sharedMemory;
  }
  if (typeof value === "string") {
    const lowered = value.toLowerCase();
    if (lowered === "shared_memory" || lowered === "shared-memory" || lowered === "sharedmemory" || lowered === "shm") {
      return BACKEND.sharedMemory;
    }
    if (lowered === "socket" || lowered === "tcp") {
      return BACKEND.socket;
    }
  }
  return value;
}

function normalizeChannelType(value) {
  if (value === undefined) {
    return CHANNEL_TYPE.fixed;
  }
  if (typeof value === "string") {
    const lowered = value.toLowerCase();
    if (lowered === "fixed") {
      return CHANNEL_TYPE.fixed;
    }
    if (lowered === "varlen" || lowered === "variable" || lowered === "variable_length" || lowered === "variable-length") {
      return CHANNEL_TYPE.varlen;
    }
  }
  return value;
}

function normalizeEndpointKind(value) {
  if (typeof value === "string") {
    const lowered = value.toLowerCase();
    if (lowered === "producer") {
      return ENDPOINT_KIND.producer;
    }
    if (lowered === "consumer") {
      return ENDPOINT_KIND.consumer;
    }
    if (lowered === "observer") {
      return ENDPOINT_KIND.observer;
    }
  }
  return value;
}

function normalizeIntegerLike(value, fieldName) {
  if (typeof value === "bigint") {
    if (value < 0n) {
      throw new RangeError(`${fieldName} must be non-negative`);
    }
    return value;
  }
  if (typeof value === "number") {
    if (!Number.isInteger(value) || value < 0) {
      throw new RangeError(`${fieldName} must be a non-negative integer`);
    }
    return value;
  }
  return value;
}

function normalizeOptions(options = {}) {
  if (options == null || typeof options !== "object" || Array.isArray(options)) {
    throw new TypeError("options must be an object");
  }

  const normalized = { ...options };
  for (const [key, value] of Object.entries(normalized)) {
    if (value === undefined) {
      delete normalized[key];
    }
  }
  normalized.backend = normalizeBackend(normalized.backend);

  const typeValue = normalized.channelType !== undefined ? normalized.channelType : normalized.type;
  normalized.channelType = normalizeChannelType(typeValue);
  delete normalized.type;

  if (normalized.shmSize !== undefined) {
    normalized.shmSize = normalizeIntegerLike(normalized.shmSize, "shmSize");
  }
  if (normalized.itemSize !== undefined) {
    normalized.itemSize = normalizeIntegerLike(normalized.itemSize, "itemSize");
  }
  if (normalized.dataAlign !== undefined) {
    normalized.dataAlign = normalizeIntegerLike(normalized.dataAlign, "dataAlign");
  }
  if (normalized.schemaId !== undefined) {
    normalized.schemaId = normalizeIntegerLike(normalized.schemaId, "schemaId");
  }
  if (normalized.socketPort !== undefined) {
    normalized.socketPort = normalizeIntegerLike(normalized.socketPort, "socketPort");
  }

  return normalized;
}

function decorateOptions(options) {
  if (!options || typeof options !== "object") {
    return options;
  }
  return {
    ...options,
    type: options.channelType,
  };
}

function requireNonEmptyString(value, fieldName) {
  if (typeof value !== "string" || value.length === 0) {
    throw new TypeError(`${fieldName} must be a non-empty string`);
  }
  return value;
}

function makeLayoutError(message, layoutError) {
  const error = new Error(message);
  error.status = STATUS.layoutError;
  error.statusCode = native.statusString(STATUS.layoutError);
  error.layoutError = layoutError;
  error.layoutErrorCode = native.layoutErrorString(layoutError);
  return error;
}

function inferExistingShmOptions(pathValue, win32ObjectNamespace) {
  return decorateOptions(_readExistingShmOptions(pathValue, win32ObjectNamespace));
}

function coerceBuffer(value, fieldName = "data") {
  if (Buffer.isBuffer(value)) {
    return value;
  }
  if (ArrayBuffer.isView(value)) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (value instanceof ArrayBuffer) {
    return Buffer.from(value);
  }
  if (typeof value === "string") {
    return Buffer.from(value);
  }
  if (Array.isArray(value)) {
    return Buffer.from(value);
  }
  throw new TypeError(`${fieldName} must be a Buffer, TypedArray, ArrayBuffer, string, or byte array`);
}

class Producer extends native.Producer {
  constructor(options = {}) {
    super(normalizeOptions(options));
  }

  sendFixedBytes(data) {
    return super.sendFixedBytes(coerceBuffer(data));
  }

  sendFixedSized(data) {
    return super.sendFixedSized(coerceBuffer(data));
  }

  sendVarlen(data) {
    return super.sendVarlen(coerceBuffer(data));
  }

  options() {
    return decorateOptions(super.options());
  }
}

class Consumer extends native.Consumer {
  constructor(options = {}) {
    super(normalizeOptions(options));
  }

  options() {
    return decorateOptions(super.options());
  }
}

class Observer extends native.Observer {
  constructor(options = {}) {
    super(normalizeOptions(options));
  }

  options() {
    return decorateOptions(super.options());
  }
}

function shmSizeForDataCapacity(dataCapacity) {
  return native.shmSizeForDataCapacity(normalizeIntegerLike(dataCapacity, "dataCapacity"));
}

function shmDataCapacityForSize(shmSize) {
  return native.shmDataCapacityForSize(normalizeIntegerLike(shmSize, "shmSize"));
}

function validateOptionsFor(kind, options) {
  return native.validateOptionsFor(normalizeEndpointKind(kind), normalizeOptions(options));
}

function makeShmEndpoints(baseOptions) {
  const producerOptions = { ...baseOptions };
  const consumerOptions = { ...baseOptions };
  const observerOptions = { ...baseOptions, createIfMissing: false };

  return Object.freeze({
    options() {
      return decorateOptions({ ...baseOptions });
    },
    openProducer() {
      return new Producer(producerOptions);
    },
    openConsumer() {
      return new Consumer(consumerOptions);
    },
    openObserver() {
      return new Observer(observerOptions);
    },
  });
}

function makeSocketListener(baseOptions) {
  return Object.freeze({
    options() {
      return decorateOptions({ ...baseOptions });
    },
    openConsumer() {
      return new Consumer(baseOptions);
    },
  });
}

function makeSocketConnector(baseOptions) {
  return Object.freeze({
    options() {
      return decorateOptions({ ...baseOptions });
    },
    openProducer() {
      return new Producer(baseOptions);
    },
  });
}

const shm = Object.freeze({
  createFixedChannel(options = {}) {
    const pathValue = requireNonEmptyString(options.path, "path");
    const baseOptions = normalizeOptions({
      path: pathValue,
      shmSize: shmSizeForDataCapacity(normalizeIntegerLike(options.dataCapacity, "dataCapacity")),
      itemSize: options.itemSize,
      dataAlign: options.dataAlign,
      schemaId: options.schemaId,
      creatorTimestampNs: options.creatorTimestampNs,
      creatorFlags: options.creatorFlags,
      win32ObjectNamespace: options.win32ObjectNamespace,
      createIfMissing: true,
      channelType: CHANNEL_TYPE.fixed,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeShmEndpoints(baseOptions);
  },

  attachFixedChannel(options = {}) {
    const pathValue = requireNonEmptyString(options.path, "path");
    const inferred = inferExistingShmOptions(pathValue, options.win32ObjectNamespace);
    if (inferred.channelType !== CHANNEL_TYPE.fixed) {
      throw makeLayoutError("attachFixedChannel: expected a fixed shared-memory channel", LAYOUT_ERROR.layoutTypeMismatch);
    }
    const baseOptions = normalizeOptions({
      ...inferred,
      path: pathValue,
      schemaId: options.schemaId !== undefined ? options.schemaId : inferred.schemaId,
      win32ObjectNamespace: options.win32ObjectNamespace !== undefined
        ? options.win32ObjectNamespace
        : inferred.win32ObjectNamespace,
      createIfMissing: false,
      channelType: CHANNEL_TYPE.fixed,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeShmEndpoints(baseOptions);
  },

  createVarlenChannel(options = {}) {
    const pathValue = requireNonEmptyString(options.path, "path");
    const baseOptions = normalizeOptions({
      path: pathValue,
      shmSize: shmSizeForDataCapacity(normalizeIntegerLike(options.dataCapacity, "dataCapacity")),
      dataAlign: options.dataAlign,
      schemaId: options.schemaId,
      creatorTimestampNs: options.creatorTimestampNs,
      creatorFlags: options.creatorFlags,
      win32ObjectNamespace: options.win32ObjectNamespace,
      createIfMissing: true,
      channelType: CHANNEL_TYPE.varlen,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeShmEndpoints(baseOptions);
  },

  attachVarlenChannel(options = {}) {
    const pathValue = requireNonEmptyString(options.path, "path");
    const inferred = inferExistingShmOptions(pathValue, options.win32ObjectNamespace);
    if (inferred.channelType !== CHANNEL_TYPE.varlen) {
      throw makeLayoutError("attachVarlenChannel: expected a varlen shared-memory channel", LAYOUT_ERROR.layoutTypeMismatch);
    }
    const baseOptions = normalizeOptions({
      ...inferred,
      path: pathValue,
      schemaId: options.schemaId !== undefined ? options.schemaId : inferred.schemaId,
      win32ObjectNamespace: options.win32ObjectNamespace !== undefined
        ? options.win32ObjectNamespace
        : inferred.win32ObjectNamespace,
      createIfMissing: false,
      channelType: CHANNEL_TYPE.varlen,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeShmEndpoints(baseOptions);
  },
});

const socket = Object.freeze({
  listenFixed(options = {}) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.fixed,
      itemSize: options.itemSize,
      socketHost: options.host,
      socketPort: options.port !== undefined ? options.port : 0,
      socketListen: true,
    });
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeSocketListener(baseOptions);
  },

  connectFixed(options = {}) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.fixed,
      itemSize: options.itemSize,
      socketHost: requireNonEmptyString(options.host, "host"),
      socketPort: options.port,
      socketListen: false,
      socketConnectRetries: options.connectRetries,
      socketConnectRetryMs: options.connectRetryMs,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    return makeSocketConnector(baseOptions);
  },

  listenVarlen(options = {}) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.varlen,
      socketHost: options.host,
      socketPort: options.port !== undefined ? options.port : 0,
      socketListen: true,
    });
    validateOptionsFor(ENDPOINT_KIND.consumer, baseOptions);
    return makeSocketListener(baseOptions);
  },

  connectVarlen(options = {}) {
    const baseOptions = normalizeOptions({
      backend: BACKEND.socket,
      channelType: CHANNEL_TYPE.varlen,
      socketHost: requireNonEmptyString(options.host, "host"),
      socketPort: options.port,
      socketListen: false,
      socketConnectRetries: options.connectRetries,
      socketConnectRetryMs: options.connectRetryMs,
    });
    validateOptionsFor(ENDPOINT_KIND.producer, baseOptions);
    return makeSocketConnector(baseOptions);
  },
});

module.exports = {
  ...native,
  Producer,
  Consumer,
  Observer,
  STATUS,
  ENDPOINT_KIND,
  BACKEND,
  CHANNEL_TYPE,
  LAYOUT_ERROR,
  shm,
  socket,
  shmSizeForDataCapacity,
  shmDataCapacityForSize,
  validateOptionsFor,
};
