from __future__ import annotations

import argparse
import os
import struct
import time

from _common import cleanup_shm, load_xproc, wait_for_message


MESSAGE_COUNT = 10


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir")
    args = parser.parse_args()

    xproc = load_xproc(args.module_dir)

    shm_path = f"/xproc_python_fixed_inprocess_{os.getpid()}"
    cleanup_shm(xproc, shm_path)

    create_options = xproc.TransportOptions()
    create_options.path = shm_path
    create_options.shm_size = xproc.shm_size_for_data_capacity(16_384)
    create_options.channel_type = xproc.ChannelType.FIXED
    create_options.item_size = 4
    create_options.create_if_missing = True

    attach_options = xproc.TransportOptions()
    attach_options.path = shm_path
    attach_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE
    attach_options.channel_type = xproc.ChannelType.FIXED
    attach_options.item_size = 4
    attach_options.create_if_missing = False

    producer = xproc.Producer(create_options)
    consumer = xproc.Consumer(attach_options)

    expected = 1

    try:
        for value in range(1, MESSAGE_COUNT + 1):
            producer.send_fixed_sized(struct.pack("<i", value))
            time.sleep(0.01)

            payload = wait_for_message(consumer.poll_copy, label="fixed-size payload")
            received = struct.unpack("<i", payload)[0]
            print(f"recv: {received}")
            if received != expected:
                raise RuntimeError(f"sequence mismatch, expected {expected} got {received}")
            expected += 1
    finally:
        consumer.close()
        producer.close()
        cleanup_shm(xproc, shm_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
