#![allow(non_camel_case_types)]

use std::ffi::{c_char, c_int, c_void};

pub const XPROC_C_INFER_EXISTING_SHM_SIZE: usize = 0;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum xproc_c_status {
    XPROC_C_STATUS_OK = 0,
    XPROC_C_STATUS_AGAIN = 1,
    XPROC_C_STATUS_BUFFER_TOO_SMALL = 2,
    XPROC_C_STATUS_INVALID_ARGUMENT = 3,
    XPROC_C_STATUS_LOGIC_ERROR = 4,
    XPROC_C_STATUS_LAYOUT_ERROR = 5,
    XPROC_C_STATUS_RUNTIME_ERROR = 6,
    XPROC_C_STATUS_NO_MEMORY = 7,
    XPROC_C_STATUS_INTERNAL_ERROR = 8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum xproc_c_endpoint_kind {
    XPROC_C_ENDPOINT_PRODUCER = 0,
    XPROC_C_ENDPOINT_CONSUMER = 1,
    XPROC_C_ENDPOINT_OBSERVER = 2,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum xproc_c_backend {
    XPROC_C_BACKEND_SHARED_MEMORY = 0,
    XPROC_C_BACKEND_SOCKET = 1,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum xproc_c_channel_type {
    XPROC_C_CHANNEL_FIXED = 0,
    XPROC_C_CHANNEL_VARLEN = 1,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum xproc_c_layout_error {
    XPROC_C_LAYOUT_ERROR_NONE = 0,
    XPROC_C_LAYOUT_ERROR_NOT_ATTACHED = 1,
    XPROC_C_LAYOUT_ERROR_BAD_MAGIC = 2,
    XPROC_C_LAYOUT_ERROR_NOT_READY_TIMEOUT = 3,
    XPROC_C_LAYOUT_ERROR_VERSION_MISMATCH = 4,
    XPROC_C_LAYOUT_ERROR_HEADER_SIZE_MISMATCH = 5,
    XPROC_C_LAYOUT_ERROR_LAYOUT_TYPE_MISMATCH = 6,
    XPROC_C_LAYOUT_ERROR_FIXED_ITEM_SIZE_MISMATCH = 7,
    XPROC_C_LAYOUT_ERROR_SCHEMA_ID_MISMATCH = 8,
    XPROC_C_LAYOUT_ERROR_ALIGNMENT_INVALID = 9,
    XPROC_C_LAYOUT_ERROR_CAPACITY_INSUFFICIENT = 10,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct xproc_c_options {
    pub backend: xproc_c_backend,
    pub path: *const c_char,
    pub shm_size: usize,
    pub item_size: u32,
    pub data_align: u32,
    pub schema_id: u64,
    pub creator_timestamp_ns: u64,
    pub creator_flags: u64,
    pub create_if_missing: c_int,
    pub channel_type: xproc_c_channel_type,
    pub win32_object_namespace: *const c_char,
    pub socket_host: *const c_char,
    pub socket_port: u16,
    pub socket_listen: c_int,
    pub socket_connect_retries: c_int,
    pub socket_connect_retry_ms: c_int,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct xproc_c_snapshot {
    pub write_pos: u64,
    pub read_pos: u64,
    pub commit_seq: u32,
    pub read_wake_seq: u32,
    pub attach_count: u32,
    pub producer_pid: i32,
}

#[repr(C)]
pub struct xproc_c_producer {
    _private: [u8; 0],
}

#[repr(C)]
pub struct xproc_c_consumer {
    _private: [u8; 0],
}

#[repr(C)]
pub struct xproc_c_observer {
    _private: [u8; 0],
}

unsafe extern "C" {
    pub fn xproc_c_options_init(options: *mut xproc_c_options);
    pub fn xproc_c_shm_size_for_data_capacity(data_capacity: usize) -> usize;
    pub fn xproc_c_shm_data_capacity_for_size(shm_size: usize) -> usize;
    pub fn xproc_c_shm_read_existing_options(
        path: *const c_char,
        win32_object_namespace: *const c_char,
        out_options: *mut xproc_c_options,
    ) -> xproc_c_status;
    pub fn xproc_c_status_string(status: xproc_c_status) -> *const c_char;
    pub fn xproc_c_layout_error_string(error: xproc_c_layout_error) -> *const c_char;
    pub fn xproc_c_version_string() -> *const c_char;
    pub fn xproc_c_current_process_id() -> i32;
    pub fn xproc_c_validate_options_for(
        kind: xproc_c_endpoint_kind,
        options: *const xproc_c_options,
    ) -> xproc_c_status;
    pub fn xproc_c_shm_unlink(path: *const c_char) -> xproc_c_status;
    pub fn xproc_c_last_error_message() -> *const c_char;
    pub fn xproc_c_last_error_copy(
        buffer: *mut c_char,
        buffer_capacity: u32,
        out_len: *mut u32,
    ) -> xproc_c_status;
    pub fn xproc_c_last_layout_error() -> xproc_c_layout_error;
    pub fn xproc_c_producer_open(
        options: *const xproc_c_options,
        out_producer: *mut *mut xproc_c_producer,
    ) -> xproc_c_status;
    pub fn xproc_c_producer_close(producer: *mut xproc_c_producer);
    pub fn xproc_c_producer_options(
        producer: *const xproc_c_producer,
        out_options: *mut xproc_c_options,
    ) -> xproc_c_status;
    pub fn xproc_c_producer_send_fixed_bytes(
        producer: *mut xproc_c_producer,
        data: *const c_void,
        payload_len: u32,
    ) -> xproc_c_status;
    pub fn xproc_c_producer_send_fixed_sized(
        producer: *mut xproc_c_producer,
        data: *const c_void,
        byte_length: u32,
    ) -> xproc_c_status;
    pub fn xproc_c_producer_send_varlen(
        producer: *mut xproc_c_producer,
        data: *const c_void,
        len: u32,
    ) -> xproc_c_status;
    pub fn xproc_c_producer_socket_port(
        producer: *const xproc_c_producer,
        out_port: *mut u16,
    ) -> xproc_c_status;
    pub fn xproc_c_consumer_open(
        options: *const xproc_c_options,
        out_consumer: *mut *mut xproc_c_consumer,
    ) -> xproc_c_status;
    pub fn xproc_c_consumer_close(consumer: *mut xproc_c_consumer);
    pub fn xproc_c_consumer_options(
        consumer: *const xproc_c_consumer,
        out_options: *mut xproc_c_options,
    ) -> xproc_c_status;
    pub fn xproc_c_consumer_pending_len(
        consumer: *const xproc_c_consumer,
        out_len: *mut u32,
    ) -> xproc_c_status;
    pub fn xproc_c_consumer_poll_copy(
        consumer: *mut xproc_c_consumer,
        buffer: *mut c_void,
        buffer_capacity: u32,
        out_len: *mut u32,
    ) -> xproc_c_status;
    pub fn xproc_c_consumer_wait(consumer: *mut xproc_c_consumer) -> xproc_c_status;
    pub fn xproc_c_consumer_socket_port(
        consumer: *const xproc_c_consumer,
        out_port: *mut u16,
    ) -> xproc_c_status;
    pub fn xproc_c_observer_open(
        options: *const xproc_c_options,
        out_observer: *mut *mut xproc_c_observer,
    ) -> xproc_c_status;
    pub fn xproc_c_observer_close(observer: *mut xproc_c_observer);
    pub fn xproc_c_observer_options(
        observer: *const xproc_c_observer,
        out_options: *mut xproc_c_options,
    ) -> xproc_c_status;
    pub fn xproc_c_observer_snapshot(
        observer: *const xproc_c_observer,
        out_snapshot: *mut xproc_c_snapshot,
    ) -> xproc_c_status;
    pub fn xproc_c_observer_peek_copy(
        observer: *mut xproc_c_observer,
        buffer: *mut c_void,
        buffer_capacity: u32,
        out_len: *mut u32,
    ) -> xproc_c_status;
}
