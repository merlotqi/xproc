use std::ffi::{CStr, CString};
use std::fmt;
use std::ptr;

use xproc_sys as sys;

pub const INFER_EXISTING_SHM_SIZE: usize = sys::XPROC_C_INFER_EXISTING_SHM_SIZE;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Backend {
    SharedMemory,
    Socket,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ChannelType {
    Fixed,
    Varlen,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EndpointKind {
    Producer,
    Consumer,
    Observer,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Ok,
    Again,
    BufferTooSmall,
    InvalidArgument,
    LogicError,
    LayoutError,
    RuntimeError,
    NoMemory,
    InternalError,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LayoutError {
    None,
    NotAttached,
    BadMagic,
    NotReadyTimeout,
    VersionMismatch,
    HeaderSizeMismatch,
    LayoutTypeMismatch,
    FixedItemSizeMismatch,
    SchemaIdMismatch,
    AlignmentInvalid,
    CapacityInsufficient,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct XprocError {
    pub status: Status,
    pub layout_error: LayoutError,
    pub message: String,
}

impl fmt::Display for XprocError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for XprocError {}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Options {
    pub backend: Backend,
    pub path: Option<String>,
    pub shm_size: usize,
    pub item_size: u32,
    pub data_align: u32,
    pub schema_id: u64,
    pub creator_timestamp_ns: u64,
    pub creator_flags: u64,
    pub create_if_missing: bool,
    pub channel_type: ChannelType,
    pub win32_object_namespace: Option<String>,
    pub socket_host: Option<String>,
    pub socket_port: u16,
    pub socket_listen: bool,
    pub socket_connect_retries: i32,
    pub socket_connect_retry_ms: i32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Snapshot {
    pub write_pos: u64,
    pub read_pos: u64,
    pub commit_seq: u32,
    pub read_wake_seq: u32,
    pub attach_count: u32,
    pub producer_pid: i32,
}

impl Default for Options {
    fn default() -> Self {
        let mut raw = unsafe { std::mem::zeroed::<sys::xproc_c_options>() };
        unsafe { sys::xproc_c_options_init(&mut raw) };
        Self::from_raw_borrowed(&raw)
    }
}

impl Options {
    pub fn with_path(mut self, path: &str) -> Self {
        self.path = Some(path.to_owned());
        self
    }

    pub fn with_channel_type(mut self, channel_type: ChannelType) -> Self {
        self.channel_type = channel_type;
        self
    }

    pub fn with_item_size(mut self, item_size: u32) -> Self {
        self.item_size = item_size;
        self
    }

    pub fn with_schema_id(mut self, schema_id: u64) -> Self {
        self.schema_id = schema_id;
        self
    }

    pub fn with_creator_timestamp_ns(mut self, creator_timestamp_ns: u64) -> Self {
        self.creator_timestamp_ns = creator_timestamp_ns;
        self
    }

    pub fn with_creator_flags(mut self, creator_flags: u64) -> Self {
        self.creator_flags = creator_flags;
        self
    }

    pub fn with_shm_size(mut self, shm_size: usize) -> Self {
        self.shm_size = shm_size;
        self
    }

    pub fn with_shm_size_for_data_capacity(self, data_capacity: usize) -> Self {
        self.with_shm_size(shm_size_for_data_capacity(data_capacity))
    }

    pub fn with_infer_existing_shm_size(mut self) -> Self {
        self.shm_size = INFER_EXISTING_SHM_SIZE;
        self
    }

    pub fn with_create_if_missing(mut self, create_if_missing: bool) -> Self {
        self.create_if_missing = create_if_missing;
        self
    }

    fn from_raw_borrowed(raw: &sys::xproc_c_options) -> Self {
        Self {
            backend: match raw.backend {
                sys::xproc_c_backend::XPROC_C_BACKEND_SOCKET => Backend::Socket,
                _ => Backend::SharedMemory,
            },
            path: string_from_ptr(raw.path),
            shm_size: raw.shm_size,
            item_size: raw.item_size,
            data_align: raw.data_align,
            schema_id: raw.schema_id,
            creator_timestamp_ns: raw.creator_timestamp_ns,
            creator_flags: raw.creator_flags,
            create_if_missing: raw.create_if_missing != 0,
            channel_type: match raw.channel_type {
                sys::xproc_c_channel_type::XPROC_C_CHANNEL_VARLEN => ChannelType::Varlen,
                _ => ChannelType::Fixed,
            },
            win32_object_namespace: string_from_ptr(raw.win32_object_namespace),
            socket_host: string_from_ptr(raw.socket_host),
            socket_port: raw.socket_port,
            socket_listen: raw.socket_listen != 0,
            socket_connect_retries: raw.socket_connect_retries,
            socket_connect_retry_ms: raw.socket_connect_retry_ms,
        }
    }

    fn to_raw(&self) -> Result<OwnedRawOptions, XprocError> {
        let mut raw = unsafe { std::mem::zeroed::<sys::xproc_c_options>() };
        unsafe { sys::xproc_c_options_init(&mut raw) };

        let path = self.path.as_deref().map(cstring).transpose()?;
        let win32_object_namespace = self.win32_object_namespace.as_deref().map(cstring).transpose()?;
        let socket_host = self.socket_host.as_deref().map(cstring).transpose()?;

        raw.backend = match self.backend {
            Backend::SharedMemory => sys::xproc_c_backend::XPROC_C_BACKEND_SHARED_MEMORY,
            Backend::Socket => sys::xproc_c_backend::XPROC_C_BACKEND_SOCKET,
        };
        raw.path = path.as_ref().map_or(ptr::null(), |value| value.as_ptr());
        raw.shm_size = self.shm_size;
        raw.item_size = self.item_size;
        raw.data_align = self.data_align;
        raw.schema_id = self.schema_id;
        raw.creator_timestamp_ns = self.creator_timestamp_ns;
        raw.creator_flags = self.creator_flags;
        raw.create_if_missing = if self.create_if_missing { 1 } else { 0 };
        raw.channel_type = match self.channel_type {
            ChannelType::Fixed => sys::xproc_c_channel_type::XPROC_C_CHANNEL_FIXED,
            ChannelType::Varlen => sys::xproc_c_channel_type::XPROC_C_CHANNEL_VARLEN,
        };
        raw.win32_object_namespace =
            win32_object_namespace.as_ref().map_or(ptr::null(), |value| value.as_ptr());
        raw.socket_host = socket_host.as_ref().map_or(ptr::null(), |value| value.as_ptr());
        raw.socket_port = self.socket_port;
        raw.socket_listen = if self.socket_listen { 1 } else { 0 };
        raw.socket_connect_retries = self.socket_connect_retries;
        raw.socket_connect_retry_ms = self.socket_connect_retry_ms;

        Ok(OwnedRawOptions {
            raw,
            _path: path,
            _win32_object_namespace: win32_object_namespace,
            _socket_host: socket_host,
        })
    }
}

struct OwnedRawOptions {
    raw: sys::xproc_c_options,
    _path: Option<CString>,
    _win32_object_namespace: Option<CString>,
    _socket_host: Option<CString>,
}

pub struct Producer {
    handle: *mut sys::xproc_c_producer,
}

pub struct Consumer {
    handle: *mut sys::xproc_c_consumer,
}

pub struct Observer {
    handle: *mut sys::xproc_c_observer,
}

impl Producer {
    pub fn open(options: &Options) -> Result<Self, XprocError> {
        let raw = options.to_raw()?;
        let mut handle = ptr::null_mut();
        let status = unsafe { sys::xproc_c_producer_open(&raw.raw, &mut handle) };
        into_result(status)?;
        Ok(Self { handle })
    }

    pub fn options(&self) -> Result<Options, XprocError> {
        let mut raw = unsafe { std::mem::zeroed::<sys::xproc_c_options>() };
        let status = unsafe { sys::xproc_c_producer_options(self.handle, &mut raw) };
        into_result(status)?;
        Ok(Options::from_raw_borrowed(&raw))
    }

    pub fn send_fixed_bytes(&self, payload: &[u8]) -> Result<(), XprocError> {
        let status = unsafe {
            sys::xproc_c_producer_send_fixed_bytes(self.handle, payload.as_ptr().cast(), payload.len() as u32)
        };
        into_result(status)
    }

    pub fn send_fixed_sized(&self, payload: &[u8]) -> Result<(), XprocError> {
        let status = unsafe {
            sys::xproc_c_producer_send_fixed_sized(self.handle, payload.as_ptr().cast(), payload.len() as u32)
        };
        into_result(status)
    }

    pub fn send_varlen(&self, payload: &[u8]) -> Result<(), XprocError> {
        let status =
            unsafe { sys::xproc_c_producer_send_varlen(self.handle, payload.as_ptr().cast(), payload.len() as u32) };
        into_result(status)
    }

    pub fn socket_port(&self) -> Result<u16, XprocError> {
        let mut out = 0;
        let status = unsafe { sys::xproc_c_producer_socket_port(self.handle, &mut out) };
        into_result(status)?;
        Ok(out)
    }
}

impl Drop for Producer {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { sys::xproc_c_producer_close(self.handle) };
        }
    }
}

impl Consumer {
    pub fn open(options: &Options) -> Result<Self, XprocError> {
        let raw = options.to_raw()?;
        let mut handle = ptr::null_mut();
        let status = unsafe { sys::xproc_c_consumer_open(&raw.raw, &mut handle) };
        into_result(status)?;
        Ok(Self { handle })
    }

    pub fn options(&self) -> Result<Options, XprocError> {
        let mut raw = unsafe { std::mem::zeroed::<sys::xproc_c_options>() };
        let status = unsafe { sys::xproc_c_consumer_options(self.handle, &mut raw) };
        into_result(status)?;
        Ok(Options::from_raw_borrowed(&raw))
    }

    pub fn pending_len(&self) -> Result<u32, XprocError> {
        let mut out = 0;
        let status = unsafe { sys::xproc_c_consumer_pending_len(self.handle, &mut out) };
        into_result(status)?;
        Ok(out)
    }

    pub fn poll_copy(&self) -> Result<Option<Vec<u8>>, XprocError> {
        poll_copy_impl(|buffer, buffer_capacity, out_len| unsafe {
            sys::xproc_c_consumer_poll_copy(self.handle, buffer, buffer_capacity, out_len)
        })
    }

    pub fn wait(&self) -> Result<(), XprocError> {
        let status = unsafe { sys::xproc_c_consumer_wait(self.handle) };
        into_result(status)
    }

    pub fn socket_port(&self) -> Result<u16, XprocError> {
        let mut out = 0;
        let status = unsafe { sys::xproc_c_consumer_socket_port(self.handle, &mut out) };
        into_result(status)?;
        Ok(out)
    }
}

impl Drop for Consumer {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { sys::xproc_c_consumer_close(self.handle) };
        }
    }
}

impl Observer {
    pub fn open(options: &Options) -> Result<Self, XprocError> {
        let raw = options.to_raw()?;
        let mut handle = ptr::null_mut();
        let status = unsafe { sys::xproc_c_observer_open(&raw.raw, &mut handle) };
        into_result(status)?;
        Ok(Self { handle })
    }

    pub fn options(&self) -> Result<Options, XprocError> {
        let mut raw = unsafe { std::mem::zeroed::<sys::xproc_c_options>() };
        let status = unsafe { sys::xproc_c_observer_options(self.handle, &mut raw) };
        into_result(status)?;
        Ok(Options::from_raw_borrowed(&raw))
    }

    pub fn snapshot(&self) -> Result<Snapshot, XprocError> {
        let mut raw = sys::xproc_c_snapshot::default();
        let status = unsafe { sys::xproc_c_observer_snapshot(self.handle, &mut raw) };
        into_result(status)?;
        Ok(Snapshot {
            write_pos: raw.write_pos,
            read_pos: raw.read_pos,
            commit_seq: raw.commit_seq,
            read_wake_seq: raw.read_wake_seq,
            attach_count: raw.attach_count,
            producer_pid: raw.producer_pid,
        })
    }

    pub fn peek_copy(&self) -> Result<Option<Vec<u8>>, XprocError> {
        poll_copy_impl(|buffer, buffer_capacity, out_len| unsafe {
            sys::xproc_c_observer_peek_copy(self.handle, buffer, buffer_capacity, out_len)
        })
    }
}

impl Drop for Observer {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { sys::xproc_c_observer_close(self.handle) };
        }
    }
}

pub fn validate_options_for(kind: EndpointKind, options: &Options) -> Result<(), XprocError> {
    let raw = options.to_raw()?;
    let status = unsafe { sys::xproc_c_validate_options_for(endpoint_kind_to_raw(kind), &raw.raw) };
    into_result(status)
}

pub fn shm_size_for_data_capacity(data_capacity: usize) -> usize {
    unsafe { sys::xproc_c_shm_size_for_data_capacity(data_capacity) }
}

pub fn shm_data_capacity_for_size(shm_size: usize) -> usize {
    unsafe { sys::xproc_c_shm_data_capacity_for_size(shm_size) }
}

pub fn shm_unlink(path: &str) -> Result<(), XprocError> {
    let path = cstring(path)?;
    let status = unsafe { sys::xproc_c_shm_unlink(path.as_ptr()) };
    into_result(status)
}

pub fn read_existing_shm_options(
    path: &str,
    win32_object_namespace: Option<&str>,
) -> Result<Options, XprocError> {
    let path = cstring(path)?;
    let win32_object_namespace = win32_object_namespace.map(cstring).transpose()?;
    let mut raw = unsafe { std::mem::zeroed::<sys::xproc_c_options>() };
    let status = unsafe {
        sys::xproc_c_shm_read_existing_options(
            path.as_ptr(),
            win32_object_namespace
                .as_ref()
                .map_or(ptr::null(), |value| value.as_ptr()),
            &mut raw,
        )
    };
    into_result(status)?;
    Ok(Options::from_raw_borrowed(&raw))
}

fn poll_copy_impl(
    mut func: impl FnMut(*mut std::ffi::c_void, u32, *mut u32) -> sys::xproc_c_status,
) -> Result<Option<Vec<u8>>, XprocError> {
    let mut required_len = 0;
    match func(ptr::null_mut(), 0, &mut required_len) {
        sys::xproc_c_status::XPROC_C_STATUS_AGAIN => return Ok(None),
        sys::xproc_c_status::XPROC_C_STATUS_OK => return Ok(Some(Vec::new())),
        sys::xproc_c_status::XPROC_C_STATUS_BUFFER_TOO_SMALL => {}
        status => return into_result(status).map(|_| None),
    }

    let mut payload = vec![0u8; required_len as usize];
    let status = func(payload.as_mut_ptr().cast(), required_len, &mut required_len);
    into_result(status)?;
    payload.truncate(required_len as usize);
    Ok(Some(payload))
}

fn cstring(value: &str) -> Result<CString, XprocError> {
    CString::new(value).map_err(|_| XprocError {
        status: Status::InvalidArgument,
        layout_error: LayoutError::None,
        message: "string contains interior NUL byte".to_owned(),
    })
}

fn string_from_ptr(ptr: *const std::ffi::c_char) -> Option<String> {
    if ptr.is_null() {
        None
    } else {
        Some(unsafe { CStr::from_ptr(ptr) }.to_string_lossy().into_owned())
    }
}

fn into_result(status: sys::xproc_c_status) -> Result<(), XprocError> {
    match status {
        sys::xproc_c_status::XPROC_C_STATUS_OK => Ok(()),
        other => Err(last_error(other)),
    }
}

fn last_error(status: sys::xproc_c_status) -> XprocError {
    let layout_error = unsafe { sys::xproc_c_last_layout_error() };
    let message = {
        let raw = unsafe { sys::xproc_c_last_error_message() };
        if raw.is_null() {
            status_string(status)
        } else {
            let value = unsafe { CStr::from_ptr(raw) }.to_string_lossy().into_owned();
            if value.is_empty() {
                status_string(status)
            } else {
                value
            }
        }
    };

    XprocError {
        status: status_from_raw(status),
        layout_error: layout_error_from_raw(layout_error),
        message,
    }
}

fn status_string(status: sys::xproc_c_status) -> String {
    let raw = unsafe { sys::xproc_c_status_string(status) };
    if raw.is_null() {
        "unknown_status".to_owned()
    } else {
        unsafe { CStr::from_ptr(raw) }.to_string_lossy().into_owned()
    }
}

fn endpoint_kind_to_raw(kind: EndpointKind) -> sys::xproc_c_endpoint_kind {
    match kind {
        EndpointKind::Producer => sys::xproc_c_endpoint_kind::XPROC_C_ENDPOINT_PRODUCER,
        EndpointKind::Consumer => sys::xproc_c_endpoint_kind::XPROC_C_ENDPOINT_CONSUMER,
        EndpointKind::Observer => sys::xproc_c_endpoint_kind::XPROC_C_ENDPOINT_OBSERVER,
    }
}

fn status_from_raw(status: sys::xproc_c_status) -> Status {
    match status {
        sys::xproc_c_status::XPROC_C_STATUS_OK => Status::Ok,
        sys::xproc_c_status::XPROC_C_STATUS_AGAIN => Status::Again,
        sys::xproc_c_status::XPROC_C_STATUS_BUFFER_TOO_SMALL => Status::BufferTooSmall,
        sys::xproc_c_status::XPROC_C_STATUS_INVALID_ARGUMENT => Status::InvalidArgument,
        sys::xproc_c_status::XPROC_C_STATUS_LOGIC_ERROR => Status::LogicError,
        sys::xproc_c_status::XPROC_C_STATUS_LAYOUT_ERROR => Status::LayoutError,
        sys::xproc_c_status::XPROC_C_STATUS_RUNTIME_ERROR => Status::RuntimeError,
        sys::xproc_c_status::XPROC_C_STATUS_NO_MEMORY => Status::NoMemory,
        sys::xproc_c_status::XPROC_C_STATUS_INTERNAL_ERROR => Status::InternalError,
    }
}

fn layout_error_from_raw(error: sys::xproc_c_layout_error) -> LayoutError {
    match error {
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_NONE => LayoutError::None,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_NOT_ATTACHED => LayoutError::NotAttached,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_BAD_MAGIC => LayoutError::BadMagic,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT => LayoutError::NotReadyTimeout,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH => LayoutError::VersionMismatch,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH => LayoutError::HeaderSizeMismatch,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH => LayoutError::LayoutTypeMismatch,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH => {
            LayoutError::FixedItemSizeMismatch
        }
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH => LayoutError::SchemaIdMismatch,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID => LayoutError::AlignmentInvalid,
        sys::xproc_c_layout_error::XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT => {
            LayoutError::CapacityInsufficient
        }
    }
}
