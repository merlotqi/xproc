from __future__ import annotations

import argparse
import os
import time

from _common import cleanup_shm, load_xproc, wait_for_message


MESSAGES = ["hello", "xproc", "variable-length", "messages"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir")
    args = parser.parse_args()

    xproc = load_xproc(args.module_dir)

    shm_path = f"/xproc_python_varlen_inprocess_{os.getpid()}"
    cleanup_shm(xproc, shm_path)

    create_options = xproc.TransportOptions()
    create_options.path = shm_path
    create_options.shm_size = xproc.shm_size_for_data_capacity(32_768)
    create_options.channel_type = xproc.ChannelType.VARLEN
    create_options.create_if_missing = True

    attach_options = xproc.TransportOptions()
    attach_options.path = shm_path
    attach_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE
    attach_options.channel_type = xproc.ChannelType.VARLEN
    attach_options.create_if_missing = False

    producer = xproc.Producer(create_options)
    consumer = xproc.Consumer(attach_options)

    received = 0

    try:
        for message in MESSAGES:
            producer.send_varlen(message)
            time.sleep(0.008)

            payload = wait_for_message(consumer.poll_copy, label="varlen payload")
            text = payload.decode("utf-8")
            print(f"recv: {text}")
            if text != MESSAGES[received]:
                raise RuntimeError(f"sequence mismatch, expected {MESSAGES[received]} got {text}")
            received += 1
    finally:
        consumer.close()
        producer.close()
        cleanup_shm(xproc, shm_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
