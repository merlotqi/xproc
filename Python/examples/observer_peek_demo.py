from __future__ import annotations

import argparse
import os
import struct

from _common import cleanup_shm, load_xproc, wait_for_message


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir")
    args = parser.parse_args()

    xproc = load_xproc(args.module_dir)

    shm_path = f"/xproc_python_observer_demo_{os.getpid()}"
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
    observer = xproc.Observer(attach_options)

    try:
        producer.send_fixed_sized(struct.pack("<i", 0x42))

        peeked = wait_for_message(observer.peek_copy, label="observer payload")
        print(f"observer sees: {struct.unpack('<i', peeked)[0]}")

        consumed = wait_for_message(consumer.poll_copy, label="consumer payload")
        print(f"consumer got: {struct.unpack('<i', consumed)[0]}, len={len(consumed)}")

        snapshot = observer.snapshot()
        print(
            "snapshot "
            f"write_pos={snapshot.write_pos} "
            f"read_pos={snapshot.read_pos} "
            f"attach_count={snapshot.attach_count}"
        )
    finally:
        observer.close()
        consumer.close()
        producer.close()
        cleanup_shm(xproc, shm_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
