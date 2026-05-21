"""
Python bindings for xproc — a high-performance Single Producer Single Consumer
(SPSC) inter-process communication library using ring buffers.

Supports shared-memory channels (fixed-size and variable-length frames) and
TCP loopback / network socket transports.

Example usage (shared-memory fixed channel)::

    import xproc

    # Create a fixed-size channel backed by shared memory
    options = xproc.TransportOptions()
    options.path = "/demo-fixed"
    options.shm_size = xproc.shm_size_for_data_capacity(4096)
    options.item_size = 4
    options.create_if_missing = True
    options.channel_type = xproc.ChannelType.FIXED

    producer = xproc.Producer(options)
    consumer = xproc.Consumer(options)
    observer = xproc.Observer(options)

    producer.send_fixed_sized(b"\\x01\\x02\\x03\\x04")

    payload = consumer.poll_copy()      # returns bytes or None
    snapshot = observer.snapshot()       # channel diagnostics

    producer.close()
    consumer.close()
    observer.close()
    xproc.shm_unlink("/demo-fixed")

Example usage (socket transport)::

    import xproc

    listener_opts = xproc.TransportOptions()
    listener_opts.backend = xproc.Backend.SOCKET
    listener_opts.socket_host = "127.0.0.1"
    listener_opts.socket_port = 0         # OS chooses ephemeral port
    listener_opts.socket_listen = True
    listener_opts.channel_type = xproc.ChannelType.VARLEN

    consumer = xproc.Consumer(listener_opts)
    port = consumer.socket_port()

    connector_opts = xproc.TransportOptions()
    connector_opts.backend = xproc.Backend.SOCKET
    connector_opts.socket_host = "127.0.0.1"
    connector_opts.socket_port = port
    connector_opts.channel_type = xproc.ChannelType.VARLEN

    producer = xproc.Producer(connector_opts)
    producer.send_varlen(b"hello over socket")
"""

from __future__ import annotations

from enum import Enum
from typing import Final


BytesLike = bytes | bytearray | memoryview | str
"""Type alias for values accepted by send methods: bytes, bytearray, memoryview, or str."""


class XprocError(RuntimeError):
    """Exception raised for xproc transport and layout failures.

    Attributes:
        status: High-level status enum (:class:`Status`), e.g. ``Status.LAYOUT_ERROR``.
        layout_error: Layout-specific error enum (:class:`LayoutError`), e.g.
            ``LayoutError.SCHEMA_ID_MISMATCH``. Only meaningful when
            ``status == Status.LAYOUT_ERROR``.
    """

    status: Status
    layout_error: LayoutError


class Status(Enum):
    """High-level result codes returned by the native xproc library."""

    OK = ...
    """Operation completed successfully."""
    AGAIN = ...
    """No message available right now; try again later."""
    BUFFER_TOO_SMALL = ...
    """The supplied buffer was too small for the pending payload."""
    INVALID_ARGUMENT = ...
    """One or more transport options were invalid."""
    LOGIC_ERROR = ...
    """Operation was attempted on a closed or incorrectly configured endpoint."""
    LAYOUT_ERROR = ...
    """A shared-memory layout validation failed (check :attr:`XprocError.layout_error`)."""
    RUNTIME_ERROR = ...
    """An unexpected platform-level error occurred (e.g. I/O failure)."""
    NO_MEMORY = ...
    """Memory allocation failed."""
    INTERNAL_ERROR = ...
    """An unexpected internal error in the native library."""


class EndpointKind(Enum):
    """Role of an endpoint for validation purposes."""

    PRODUCER = ...
    """Endpoint that writes messages into the channel."""
    CONSUMER = ...
    """Endpoint that reads messages from the channel."""
    OBSERVER = ...
    """Read-only endpoint that peeks without consuming."""


class Backend(Enum):
    """Transport backend selection."""

    SHARED_MEMORY = ...
    """Shared-memory ring buffer (default)."""
    SOCKET = ...
    """TCP socket transport (loopback or network)."""


class ChannelType(Enum):
    """Frame format for the channel."""

    FIXED = ...
    """Every message has the same byte length (``item_size``)."""
    VARLEN = ...
    """Messages have variable length, prefixed with a length header."""


class LayoutError(Enum):
    """Detailed error codes for manifest / layout validation failures.

    Only meaningful when :attr:`XprocError.status` is ``Status.LAYOUT_ERROR``.
    """

    NONE = ...
    """No layout error."""
    NOT_ATTACHED = ...
    """The shared-memory segment is not yet attached."""
    BAD_MAGIC = ...
    """The segment magic number does not match xproc expectations."""
    NOT_READY_TIMEOUT = ...
    """Timed out waiting for the creator to finish initialization."""
    VERSION_MISMATCH = ...
    """The segment was created by an incompatible xproc version."""
    HEADER_SIZE_MISMATCH = ...
    """The manifest header size does not match this build of xproc."""
    LAYOUT_TYPE_MISMATCH = ...
    """The channel type (fixed vs. varlen) does not match."""
    FIXED_ITEM_SIZE_MISMATCH = ...
    """The fixed item size in the manifest does not match the requested size."""
    SCHEMA_ID_MISMATCH = ...
    """The schema ID in the manifest does not match the requested ID."""
    ALIGNMENT_INVALID = ...
    """The requested data alignment is not satisfied by the segment layout."""
    CAPACITY_INSUFFICIENT = ...
    """The requested data capacity exceeds the available segment size."""


class TransportOptions:
    """Configuration for opening a channel endpoint.

    All fields have sensible defaults (shared-memory, fixed-size channel,
    ``create_if_missing = False``). Set fields before passing to
    :class:`Producer`, :class:`Consumer`, or :class:`Observer` constructors.

    Example::

        opts = xproc.TransportOptions()
        opts.backend = xproc.Backend.SHARED_MEMORY
        opts.path = "/my-channel"
        opts.shm_size = xproc.shm_size_for_data_capacity(16384)
        opts.item_size = 8
        opts.create_if_missing = True
        opts.channel_type = xproc.ChannelType.FIXED
        producer = xproc.Producer(opts)

    Field reference:
    """

    backend: Backend
    """Transport backend. Default: :attr:`Backend.SHARED_MEMORY`."""

    path: str | None
    """For shared memory: the shm object path (e.g. ``"/demo-fixed"``).
    For sockets: unused. Default: ``None``."""

    shm_size: int
    """Total shared-memory segment size in bytes. Use
    :func:`shm_size_for_data_capacity` to derive this from the desired payload
    capacity. For attaching to an existing channel, set this to
    :data:`INFER_EXISTING_SHM_SIZE` to auto-detect. Default: ``0``."""

    item_size: int
    """Number of bytes in each fixed-size frame. Required for
    :attr:`ChannelType.FIXED` channels; ignored for varlen. Default: ``0``."""

    data_align: int
    """Payload alignment in bytes. ``0`` means the native default. Default: ``0``."""

    schema_id: int
    """Application-defined manifest identifier validated on attach.
    Set to matching values on create and attach to detect mismatches.
    Default: ``0``."""

    creator_timestamp_ns: int
    """Opaque nanosecond timestamp recorded in the manifest by the creator.
    Used for diagnostics. Default: ``0``."""

    creator_flags: int
    """Opaque flags recorded in the manifest by the creator.
    Used for diagnostics. Default: ``0``."""

    create_if_missing: bool
    """If ``True``, create a new shared-memory segment when the path does not
    exist. If ``False``, attach to an existing segment. Default: ``False``."""

    channel_type: ChannelType
    """Channel frame format. Default: :attr:`ChannelType.FIXED`."""

    win32_object_namespace: str | None
    """Windows-only: object namespace prefix for the shared-memory mapping.
    Ignored on non-Windows platforms. Default: ``None``."""

    socket_host: str | None
    """For sockets: hostname or IP to bind (listen) or connect to.
    Default: ``None`` (platform socket default)."""

    socket_port: int
    """For sockets: TCP port. Set to ``0`` on listen to let the OS choose an
    ephemeral port, then read back via :meth:`Producer.socket_port` or
    :meth:`Consumer.socket_port`. Default: ``0``."""

    socket_listen: bool
    """For sockets: ``True`` to listen for incoming connections, ``False`` to
    connect to a remote listener. Default: ``False``."""

    socket_connect_retries: int
    """For sockets: number of connection attempts before failing.
    ``0`` means no retries. Default: ``0``."""

    socket_connect_retry_ms: int
    """For sockets: delay in milliseconds between connection retries.
    Default: ``0``."""

    def __init__(self) -> None:
        """Initialise with default transport options."""
        ...
    def __repr__(self) -> str: ...


class Snapshot:
    """Immutable snapshot of channel counters and metadata.

    Returned by :meth:`Observer.snapshot` for diagnostics and monitoring.
    """

    write_pos: int
    """Current producer write position in the ring buffer."""
    read_pos: int
    """Current consumer read position in the ring buffer."""
    commit_seq: int
    """Monotonically increasing commit sequence number."""
    read_wake_seq: int
    """Read wake sequence number used for wait/wake signalling."""
    attach_count: int
    """Number of endpoints currently attached to the channel."""
    producer_pid: int
    """OS process ID of the producer that created the channel."""

    def __repr__(self) -> str: ...


class Producer:
    """Producer endpoint that writes messages into a channel.

    Supports fixed-size frames (:meth:`send_fixed_sized`), fixed-size with
    slicing (:meth:`send_fixed_bytes`), and variable-length messages
    (:meth:`send_varlen`).

    Can be used as a context manager::

        with xproc.Producer(opts) as producer:
            producer.send_varlen(b"payload")
        # automatically closed
    """

    def __init__(self, options: TransportOptions) -> None:
        """Open the producer with the given transport configuration.

        Raises :class:`XprocError` if the options are invalid or the
        underlying transport cannot be opened.
        """
        ...
    def close(self) -> None:
        """Release the native endpoint handle. Idempotent."""
        ...
    def options(self) -> TransportOptions:
        """Return a copy of the resolved transport options used by this endpoint."""
        ...
    def send_fixed_bytes(self, value: BytesLike) -> None:
        """Send a payload into a fixed-size channel.

        The payload may be shorter than ``item_size`` (remaining bytes are
        zero-padded). For an exact-size check, use :meth:`send_fixed_sized`.

        Raises :class:`XprocError` on transport failure.
        """
        ...
    def send_fixed_sized(self, value: BytesLike) -> None:
        """Send one fixed-size frame and validate ``len(value) == item_size``.

        Raises:
            XprocError: if the length does not match ``item_size`` or the
                transport fails.
        """
        ...
    def send_varlen(self, value: BytesLike) -> None:
        """Send one variable-length payload.

        Raises :class:`XprocError` on transport failure.
        """
        ...
    def socket_port(self) -> int:
        """Return the bound or connected TCP port for socket-backed producers.

        For listening sockets, this is the actual port assigned by the OS
        (useful when ``socket_port`` was ``0``).
        """
        ...
    def __enter__(self) -> Producer: ...
    def __exit__(self, exc_type: object, exc: object, tb: object) -> None: ...


class Consumer:
    """Consumer endpoint that reads messages from a channel.

    Supports non-blocking poll (:meth:`poll_copy`) and blocking wait
    (:meth:`wait`).

    Can be used as a context manager::

        with xproc.Consumer(opts) as consumer:
            consumer.wait()
            payload = consumer.poll_copy()
        # automatically closed
    """

    def __init__(self, options: TransportOptions) -> None:
        """Open the consumer with the given transport configuration.

        Raises :class:`XprocError` if the options are invalid or the
        underlying transport cannot be opened.
        """
        ...
    def close(self) -> None:
        """Release the native endpoint handle. Idempotent."""
        ...
    def options(self) -> TransportOptions:
        """Return a copy of the resolved transport options used by this endpoint."""
        ...
    def pending_len(self) -> int:
        """Return the byte length of the pending message, or ``0`` if empty.

        For fixed-size channels this returns ``item_size`` when data is
        available; for variable-length channels it returns the actual payload
        size.
        """
        ...
    def poll_copy(self) -> bytes | None:
        """Non-blocking read of the next available message.

        Returns:
            The payload as ``bytes``, or ``None`` if no message is currently
            available. Call :meth:`wait` first to block until data arrives.
        """
        ...
    def wait(self) -> None:
        """Block until data is available or the transport wakes the consumer.

        Releases the GIL during the wait so other Python threads can run.

        Raises :class:`XprocError` on transport failure.
        """
        ...
    def socket_port(self) -> int:
        """Return the bound TCP port for socket-backed consumers.

        For listening sockets, this is the actual port assigned by the OS.
        """
        ...
    def __enter__(self) -> Consumer: ...
    def __exit__(self, exc_type: object, exc: object, tb: object) -> None: ...


class Observer:
    """Read-only observer that peeks at the latest committed message without
    consuming it.

    Useful for monitoring, diagnostics, or fan-out scenarios where multiple
    readers need to see the same data.

    Can be used as a context manager::

        with xproc.Observer(opts) as observer:
            snap = observer.snapshot()
            latest = observer.peek_copy()
        # automatically closed
    """

    def __init__(self, options: TransportOptions) -> None:
        """Open the observer with the given transport configuration.

        The observer always attaches with ``create_if_missing = False``
        regardless of the passed options.

        Raises :class:`XprocError` if the options are invalid or the
        underlying transport cannot be opened.
        """
        ...
    def close(self) -> None:
        """Release the native endpoint handle. Idempotent."""
        ...
    def options(self) -> TransportOptions:
        """Return a copy of the resolved transport options used by this endpoint."""
        ...
    def snapshot(self) -> Snapshot:
        """Return a snapshot of channel positions and metadata for diagnostics.

        The returned :class:`Snapshot` includes write/read positions, commit
        sequences, attach count, and producer PID.
        """
        ...
    def peek_copy(self) -> bytes | None:
        """Return the latest committed payload without consuming it.

        Returns:
            The payload as ``bytes``, or ``None`` if no message is currently
            available. Unlike :meth:`Consumer.poll_copy`, the message remains
            visible to subsequent peek calls and the consumer.
        """
        ...
    def __enter__(self) -> Observer: ...
    def __exit__(self, exc_type: object, exc: object, tb: object) -> None: ...


# ---------------------------------------------------------------------------
# Sentinel
# ---------------------------------------------------------------------------

INFER_EXISTING_SHM_SIZE: Final[int]
"""Sentinel value for :attr:`TransportOptions.shm_size` that tells the attach
path to auto-detect the segment size from the existing manifest.

Example::

    opts = xproc.TransportOptions()
    opts.path = "/existing-channel"
    opts.shm_size = xproc.INFER_EXISTING_SHM_SIZE
    opts.item_size = 8
    consumer = xproc.Consumer(opts)  # size read from manifest
"""

# ---------------------------------------------------------------------------
# Package version
# ---------------------------------------------------------------------------

__version__: str
"""The linked xproc library version string (e.g. ``"1.1.0"``)."""

# ---------------------------------------------------------------------------
# Status constants
# ---------------------------------------------------------------------------

OK: Final[Status]
"""Alias for :attr:`Status.OK`."""
AGAIN: Final[Status]
"""Alias for :attr:`Status.AGAIN`."""
BUFFER_TOO_SMALL: Final[Status]
"""Alias for :attr:`Status.BUFFER_TOO_SMALL`."""
INVALID_ARGUMENT: Final[Status]
"""Alias for :attr:`Status.INVALID_ARGUMENT`."""
LOGIC_ERROR: Final[Status]
"""Alias for :attr:`Status.LOGIC_ERROR`."""
LAYOUT_ERROR: Final[Status]
"""Alias for :attr:`Status.LAYOUT_ERROR`."""
RUNTIME_ERROR: Final[Status]
"""Alias for :attr:`Status.RUNTIME_ERROR`."""
NO_MEMORY: Final[Status]
"""Alias for :attr:`Status.NO_MEMORY`."""
INTERNAL_ERROR: Final[Status]
"""Alias for :attr:`Status.INTERNAL_ERROR`."""

# ---------------------------------------------------------------------------
# EndpointKind constants
# ---------------------------------------------------------------------------

PRODUCER: Final[EndpointKind]
"""Alias for :attr:`EndpointKind.PRODUCER`."""
CONSUMER: Final[EndpointKind]
"""Alias for :attr:`EndpointKind.CONSUMER`."""
OBSERVER: Final[EndpointKind]
"""Alias for :attr:`EndpointKind.OBSERVER`."""

# ---------------------------------------------------------------------------
# Backend constants
# ---------------------------------------------------------------------------

SHARED_MEMORY: Final[Backend]
"""Alias for :attr:`Backend.SHARED_MEMORY`."""
SOCKET: Final[Backend]
"""Alias for :attr:`Backend.SOCKET`."""

# ---------------------------------------------------------------------------
# ChannelType constants
# ---------------------------------------------------------------------------

FIXED: Final[ChannelType]
"""Alias for :attr:`ChannelType.FIXED`."""
VARLEN: Final[ChannelType]
"""Alias for :attr:`ChannelType.VARLEN`."""

# ---------------------------------------------------------------------------
# LayoutError constants
# ---------------------------------------------------------------------------

NONE: Final[LayoutError]
"""Alias for :attr:`LayoutError.NONE`."""
NOT_ATTACHED: Final[LayoutError]
"""Alias for :attr:`LayoutError.NOT_ATTACHED`."""
BAD_MAGIC: Final[LayoutError]
"""Alias for :attr:`LayoutError.BAD_MAGIC`."""
NOT_READY_TIMEOUT: Final[LayoutError]
"""Alias for :attr:`LayoutError.NOT_READY_TIMEOUT`."""
VERSION_MISMATCH: Final[LayoutError]
"""Alias for :attr:`LayoutError.VERSION_MISMATCH`."""
HEADER_SIZE_MISMATCH: Final[LayoutError]
"""Alias for :attr:`LayoutError.HEADER_SIZE_MISMATCH`."""
LAYOUT_TYPE_MISMATCH: Final[LayoutError]
"""Alias for :attr:`LayoutError.LAYOUT_TYPE_MISMATCH`."""
FIXED_ITEM_SIZE_MISMATCH: Final[LayoutError]
"""Alias for :attr:`LayoutError.FIXED_ITEM_SIZE_MISMATCH`."""
SCHEMA_ID_MISMATCH: Final[LayoutError]
"""Alias for :attr:`LayoutError.SCHEMA_ID_MISMATCH`."""
ALIGNMENT_INVALID: Final[LayoutError]
"""Alias for :attr:`LayoutError.ALIGNMENT_INVALID`."""
CAPACITY_INSUFFICIENT: Final[LayoutError]
"""Alias for :attr:`LayoutError.CAPACITY_INSUFFICIENT`."""

# ---------------------------------------------------------------------------
# Functions
# ---------------------------------------------------------------------------

def shm_size_for_data_capacity(data_capacity: int) -> int:
    """Convert a desired payload capacity (bytes) to the required shared-memory segment size.

    The result includes xproc's internal ring-buffer metadata overhead. Use
    this to set :attr:`TransportOptions.shm_size` when creating a channel::

        opts.shm_size = xproc.shm_size_for_data_capacity(16384)
    """
    ...

def shm_data_capacity_for_size(shm_size: int) -> int:
    """Convert a shared-memory segment size back to usable payload capacity.

    Inverse of :func:`shm_size_for_data_capacity`. Useful for inspecting an
    existing segment's capacity.
    """
    ...

def status_string(status: Status) -> str:
    """Return the human-readable name for a status code (e.g. ``"layout_error"``)."""
    ...

def layout_error_string(error: LayoutError) -> str:
    """Return the human-readable name for a layout error code (e.g. ``"schema_id_mismatch"``)."""
    ...

def version_string() -> str:
    """Return the linked xproc C library version string (e.g. ``"1.1.0"``)."""
    ...

def current_process_id() -> int:
    """Return the current OS process ID as observed by the native binding."""
    ...

def last_error_message() -> str:
    """Return the last human-readable error message from the native library.

    Most useful for debugging after catching an :class:`XprocError`.
    """
    ...

def last_layout_error() -> LayoutError:
    """Return the last layout error code from the native library.

    Most useful for debugging after catching an :class:`XprocError`.
    """
    ...

def validate_options_for(kind: EndpointKind, options: TransportOptions) -> None:
    """Validate transport options for the given endpoint kind.

    Raises :class:`XprocError` if the options are invalid for that kind.
    Useful for catching misconfigurations early, before opening endpoints::

        opts = xproc.TransportOptions()
        opts.path = "/demo"
        opts.shm_size = xproc.shm_size_for_data_capacity(4096)
        opts.item_size = 4
        opts.create_if_missing = True
        opts.channel_type = xproc.ChannelType.FIXED
        xproc.validate_options_for(xproc.EndpointKind.PRODUCER, opts)

    Args:
        kind: The endpoint role to validate against
            (:attr:`EndpointKind.PRODUCER`, :attr:`EndpointKind.CONSUMER`,
            or :attr:`EndpointKind.OBSERVER`).
        options: The transport options to validate.
    """
    ...

def shm_unlink(path: str) -> None:
    """Remove a shared-memory segment by path.

    Useful for test cleanup and local demos. Raises :class:`XprocError` if
    the segment does not exist or cannot be removed::

        try:
            xproc.shm_unlink("/my-channel")
        except xproc.XprocError:
            pass  # already removed or never created
    """
    ...
