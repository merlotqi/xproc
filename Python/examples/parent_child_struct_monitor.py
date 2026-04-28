from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import time

from _common import cleanup_shm, load_xproc, module_dir_for_child, non_negative_int


CHILD_FLAG = "--pc-struct-child"
MESSAGE_BYTES = 256
INT32_BYTES = 4
PACKET_SIZE = MESSAGE_BYTES + (INT32_BYTES * 2)
DATA_CAPACITY = 32_768
POLL_INTERVAL_MS = 100


def create_producer_options(xproc, shm_path: str):
    options = xproc.TransportOptions()
    options.path = shm_path
    options.shm_size = xproc.INFER_EXISTING_SHM_SIZE
    options.channel_type = xproc.ChannelType.FIXED
    options.item_size = PACKET_SIZE
    options.create_if_missing = False
    return options


def create_consumer_options(xproc, shm_path: str):
    options = xproc.TransportOptions()
    options.path = shm_path
    options.shm_size = xproc.shm_size_for_data_capacity(DATA_CAPACITY)
    options.channel_type = xproc.ChannelType.FIXED
    options.item_size = PACKET_SIZE
    options.create_if_missing = True
    return options


def encode_packet(message: str, a: int, b: int) -> bytes:
    packet = bytearray(PACKET_SIZE)
    encoded_message = message.encode("utf-8")
    packet[: min(len(encoded_message), MESSAGE_BYTES - 1)] = encoded_message[: MESSAGE_BYTES - 1]
    struct.pack_into("<ii", packet, MESSAGE_BYTES, a, b)
    return bytes(packet)


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


def run_child_writer(xproc, shm_path: str, ticks: int, interval_ms: int) -> None:
    producer = xproc.Producer(create_producer_options(xproc, shm_path))
    try:
        for index in range(ticks + 1):
            producer.send_fixed_sized(encode_packet(f"tick-{index}", index, index * 2))
            time.sleep(interval_ms / 1000.0)
    finally:
        producer.close()


def run_parent(xproc, ticks: int, interval_ms: int) -> None:
    shm_path = f"/xproc_example_parent_child_struct_{os.getpid()}"
    cleanup_shm(xproc, shm_path)

    consumer = xproc.Consumer(create_consumer_options(xproc, shm_path))
    last_packet_bytes: bytes | None = None

    child_argv = [
        sys.executable,
        str(os.path.abspath(__file__)),
        CHILD_FLAG,
        shm_path,
        f"--ticks={ticks}",
        f"--interval-ms={interval_ms}",
    ]
    child_module_dir = module_dir_for_child(xproc)
    if child_module_dir is not None:
        child_argv.extend(["--module-dir", child_module_dir])

    child = subprocess.Popen(child_argv)

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
        raise RuntimeError("child process failed")

    print("child exited, parent done")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(CHILD_FLAG, dest="child_shm_path")
    parser.add_argument("--module-dir")
    parser.add_argument("--ticks", type=non_negative_int, default=100)
    parser.add_argument("--interval-ms", type=non_negative_int, default=300)
    args = parser.parse_args()

    xproc = load_xproc(args.module_dir)

    if args.child_shm_path is not None:
        run_child_writer(xproc, args.child_shm_path, args.ticks, args.interval_ms)
        return 0

    run_parent(xproc, args.ticks, args.interval_ms)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
