from __future__ import annotations

import argparse
import os
import sys
import time
import uuid
from pathlib import Path

if not __debug__:
    raise RuntimeError("smoke tests must not run with python -O")


def make_fixed_options(
    xproc: object,
    path: str,
    item_size: int,
    schema_id: int,
    *,
    create_if_missing: bool,
    creator_timestamp_ns: int = 0,
    creator_flags: int = 0,
) -> object:
    options = xproc.TransportOptions()
    options.path = path
    options.shm_size = xproc.shm_size_for_data_capacity(4096)
    options.item_size = item_size
    options.create_if_missing = create_if_missing
    options.channel_type = xproc.ChannelType.FIXED
    options.schema_id = schema_id
    options.creator_timestamp_ns = creator_timestamp_ns
    options.creator_flags = creator_flags
    return options


def run_smoke_checks(xproc: object) -> None:
    assert xproc.version_string()
    assert xproc.status_string(xproc.Status.OK) == "ok"
    assert xproc.layout_error_string(xproc.LayoutError.NONE) == "none"
    defaults = xproc.TransportOptions()
    assert defaults.creator_timestamp_ns == 0
    assert defaults.creator_flags == 0
    assert "creator_timestamp_ns=0" in repr(defaults)
    assert "creator_flags=0" in repr(defaults)

    payload = b"hello from python"
    shm_path = f"/xproc_py_smoke_{os.getpid()}_{uuid.uuid4().hex}"
    persisted_creator_timestamp_ns = 0x1122334455667788
    persisted_creator_flags = 0x8877665544332211
    create_options = make_fixed_options(
        xproc,
        shm_path,
        len(payload),
        0,
        create_if_missing=True,
        creator_timestamp_ns=persisted_creator_timestamp_ns,
        creator_flags=persisted_creator_flags,
    )

    attach_options = make_fixed_options(
        xproc,
        shm_path,
        len(payload),
        0,
        create_if_missing=False,
        creator_timestamp_ns=0x0102030405060708,
        creator_flags=0xAABBCCDDEEFF0011,
    )
    attach_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE

    xproc.validate_options_for(xproc.EndpointKind.CONSUMER, create_options)
    xproc.validate_options_for(xproc.EndpointKind.PRODUCER, attach_options)

    consumer = None
    observer = None
    producer = None

    try:
        consumer = xproc.Consumer(create_options)
        observer = xproc.Observer(attach_options)
        producer = xproc.Producer(attach_options)
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

        producer_borrowed = producer.options()
        consumer_borrowed = consumer.options()
        observer_borrowed = observer.options()
        assert producer_borrowed.creator_timestamp_ns == persisted_creator_timestamp_ns
        assert producer_borrowed.creator_flags == persisted_creator_flags
        assert consumer_borrowed.creator_timestamp_ns == persisted_creator_timestamp_ns
        assert consumer_borrowed.creator_flags == persisted_creator_flags
        assert observer_borrowed.creator_timestamp_ns == persisted_creator_timestamp_ns
        assert observer_borrowed.creator_flags == persisted_creator_flags
    finally:
        if producer is not None:
            producer.close()
        if observer is not None:
            observer.close()
        if consumer is not None:
            consumer.close()
        try:
            xproc.shm_unlink(shm_path)
        except xproc.XprocError:
            pass

    mismatch_path = f"/xproc_py_schema_mismatch_{os.getpid()}_{uuid.uuid4().hex}"
    producer_options = make_fixed_options(
        xproc, mismatch_path, 4, 7, create_if_missing=True
    )
    consumer_options = make_fixed_options(
        xproc, mismatch_path, 4, 8, create_if_missing=False
    )
    consumer_options.shm_size = xproc.INFER_EXISTING_SHM_SIZE

    producer = None
    consumer = None
    try:
        producer = xproc.Producer(producer_options)
        try:
            consumer = xproc.Consumer(consumer_options)
        except xproc.XprocError as exc:
            assert exc.status == xproc.Status.LAYOUT_ERROR
            assert exc.layout_error == xproc.LayoutError.SCHEMA_ID_MISMATCH
            assert "schema_id mismatch" in str(exc).lower()
        else:
            raise AssertionError("expected schema mismatch to raise XprocError")
    finally:
        if consumer is not None:
            consumer.close()
        if producer is not None:
            producer.close()
        try:
            xproc.shm_unlink(mismatch_path)
        except xproc.XprocError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir", required=True)
    args = parser.parse_args()

    module_dir = Path(args.module_dir).resolve()
    sys.path.insert(0, str(module_dir))

    import xproc

    module_file = Path(xproc.__file__).resolve()
    assert module_file.is_relative_to(module_dir), (
        f"expected xproc to load from {module_dir}, got {module_file}"
    )

    run_smoke_checks(xproc)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
