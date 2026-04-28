from __future__ import annotations

import argparse
import os
import struct
import subprocess
import time

from _common import cleanup_shm, find_child_binary, load_xproc, non_negative_int


MESSAGE_BYTES = 256
INT32_BYTES = 4
PACKET_SIZE = MESSAGE_BYTES + (INT32_BYTES * 2)
DATA_CAPACITY = 32_768
POLL_INTERVAL_MS = 100
SCHEMA_ID = 0x4E4F44455F435050  # "NODE_CPP"


def create_consumer_options(xproc, shm_path: str):
    options = xproc.TransportOptions()
    options.path = shm_path
    options.shm_size = xproc.shm_size_for_data_capacity(DATA_CAPACITY)
    options.channel_type = xproc.ChannelType.FIXED
    options.item_size = PACKET_SIZE
    options.schema_id = SCHEMA_ID
    options.create_if_missing = True
    return options


def decode_packet(payload: bytes) -> tuple[str, int, int]:
    message_end = payload.find(b"\0", 0, MESSAGE_BYTES)
    if message_end < 0:
        message_end = MESSAGE_BYTES

    message = payload[:message_end].decode("utf-8")
    a, b = struct.unpack_from("<ii", payload, MESSAGE_BYTES)
    return message, a, b


def drain_consumer(consumer, on_packet) -> None:
    while True:
        payload = consumer.poll_copy()
        if payload is None:
            return
        if len(payload) != PACKET_SIZE:
            continue
        on_packet(payload)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir")
    parser.add_argument("--child-bin")
    parser.add_argument("--ticks", type=non_negative_int, default=100)
    parser.add_argument("--interval-ms", type=non_negative_int, default=300)
    args = parser.parse_args()

    xproc = load_xproc(args.module_dir)
    child_bin = find_child_binary(args.child_bin)
    shm_path = f"/xproc_python_parent_cpp_child_struct_{os.getpid()}"

    cleanup_shm(xproc, shm_path)

    consumer = xproc.Consumer(create_consumer_options(xproc, shm_path))
    last_packet_bytes: bytes | None = None
    child = subprocess.Popen(
        [
            str(child_bin),
            "--shm-path",
            shm_path,
            "--ticks",
            str(args.ticks),
            "--interval-ms",
            str(args.interval_ms),
        ]
    )

    try:
        while child.poll() is None:
            def print_packet(payload: bytes) -> None:
                nonlocal last_packet_bytes
                if last_packet_bytes == payload:
                    return

                message, a, b = decode_packet(payload)
                print(f"message={message}, a={a}, b={b}")
                last_packet_bytes = bytes(payload)

            drain_consumer(consumer, print_packet)
            time.sleep(POLL_INTERVAL_MS / 1000.0)

        def print_packet(payload: bytes) -> None:
            nonlocal last_packet_bytes
            if last_packet_bytes == payload:
                return

            message, a, b = decode_packet(payload)
            print(f"message={message}, a={a}, b={b}")
            last_packet_bytes = bytes(payload)

        drain_consumer(consumer, print_packet)
    finally:
        consumer.close()
        cleanup_shm(xproc, shm_path)

    if child.returncode != 0:
        raise RuntimeError("C++ child process failed")

    print("child exited, parent done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
