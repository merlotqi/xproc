from __future__ import annotations

import argparse
import os
import sys
import time
import uuid
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir", required=True)
    args = parser.parse_args()

    sys.path.insert(0, str(Path(args.module_dir).resolve()))

    import xproc

    assert xproc.version_string()
    assert xproc.status_string(xproc.Status.OK) == "ok"
    assert xproc.layout_error_string(xproc.LayoutError.NONE) == "none"

    payload = b"hello from python"
    shm_path = f"/xproc_py_smoke_{os.getpid()}_{uuid.uuid4().hex}"
    create_options = xproc.TransportOptions()
    create_options.path = shm_path
    create_options.shm_size = xproc.shm_size_for_data_capacity(4096)
    create_options.item_size = len(payload)
    create_options.create_if_missing = True
    create_options.channel_type = xproc.ChannelType.FIXED

    attach_options = xproc.TransportOptions()
    attach_options.path = shm_path
    attach_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE
    attach_options.item_size = len(payload)
    attach_options.create_if_missing = False
    attach_options.channel_type = xproc.ChannelType.FIXED

    xproc.validate_options_for(xproc.EndpointKind.CONSUMER, create_options)
    xproc.validate_options_for(xproc.EndpointKind.PRODUCER, attach_options)

    consumer = xproc.Consumer(create_options)
    observer = xproc.Observer(attach_options)
    producer = xproc.Producer(attach_options)

    try:
        producer.send_fixed_sized(payload)

        observed = None
        for _ in range(100):
            observed = observer.peek_copy()
            if observed is not None:
                break
            time.sleep(0.01)
        assert observed == payload

        received = None
        for _ in range(100):
            received = consumer.poll_copy()
            if received is not None:
                break
            time.sleep(0.01)
        assert received == payload

        snapshot = observer.snapshot()
        assert snapshot.attach_count >= 2
        assert consumer.pending_len() == 0
    finally:
        producer.close()
        observer.close()
        consumer.close()
        xproc.shm_unlink(shm_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
