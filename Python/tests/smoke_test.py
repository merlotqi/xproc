from __future__ import annotations

import argparse
import os
import sys
import time
import uuid
from pathlib import Path


def make_fixed_options(path: str, item_size: int, schema_id: int, *, create_if_missing: bool) -> "xproc.TransportOptions":
    import xproc

    options = xproc.TransportOptions()
    options.path = path
    options.shm_size = xproc.shm_size_for_data_capacity(4096)
    options.item_size = item_size
    options.create_if_missing = create_if_missing
    options.channel_type = xproc.ChannelType.FIXED
    options.schema_id = schema_id
    return options


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
    create_options = make_fixed_options(shm_path, len(payload), 0, create_if_missing=True)

    attach_options = make_fixed_options(shm_path, len(payload), 0, create_if_missing=False)
    attach_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE

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

    mismatch_path = f"/xproc_py_schema_mismatch_{os.getpid()}_{uuid.uuid4().hex}"
    producer_options = make_fixed_options(mismatch_path, 4, 7, create_if_missing=True)
    consumer_options = make_fixed_options(mismatch_path, 4, 8, create_if_missing=False)
    consumer_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE

    producer = xproc.Producer(producer_options)
    try:
        try:
            xproc.Consumer(consumer_options)
        except xproc.XprocError as exc:
            assert exc.status == xproc.Status.LAYOUT_ERROR
            assert exc.layout_error == xproc.LayoutError.SCHEMA_ID_MISMATCH
            assert "schema_id mismatch" in str(exc).lower()
        else:
            raise AssertionError("expected schema mismatch to raise XprocError")
    finally:
        producer.close()
        xproc.shm_unlink(mismatch_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
