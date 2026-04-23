from __future__ import annotations

import argparse
import time

from _common import load_xproc


UPSTREAM_SCHEMA_ID = 0x5059455654303031  # "PYEVT001"
DOWNSTREAM_SCHEMA_ID = 0x5059435452303031  # "PYCTR001"
PROTOCOL_VERSION = "1"
HANDSHAKE_TIMEOUT_S = 10.0
POLL_DELAY_S = 0.02


def escape_field(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace("\t", "\\t")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("=", "\\e")
    )


def unescape_field(value: str) -> str:
    out: list[str] = []
    escaping = False
    for ch in value:
        if not escaping:
            if ch == "\\":
                escaping = True
            else:
                out.append(ch)
            continue

        out.append(
            {
                "\\": "\\",
                "t": "\t",
                "n": "\n",
                "r": "\r",
                "e": "=",
            }.get(ch, ch)
        )
        escaping = False

    if escaping:
        out.append("\\")
    return "".join(out)


def build_message(message_type: str, **fields: object) -> str:
    parts = [message_type]
    for key, value in fields.items():
        parts.append(f"{key}={escape_field(str(value))}")
    return "\t".join(parts)


def parse_message(raw: bytes) -> tuple[str, dict[str, str]]:
    text = raw.decode("utf-8")
    parts = text.split("\t")
    message_type = parts[0] if parts else ""
    fields: dict[str, str] = {}
    for token in parts[1:]:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = unescape_field(value)
    return message_type, fields


def make_varlen_options(xproc, path: str, create_if_missing: bool, schema_id: int):
    options = xproc.TransportOptions()
    options.path = path
    options.shm_size = (
        xproc.shm_size_for_data_capacity(65536)
        if create_if_missing
        else xproc.INFER_EXISTING_SHM_SIZE
    )
    options.channel_type = xproc.ChannelType.VARLEN
    options.schema_id = schema_id
    options.create_if_missing = create_if_missing
    return options


def send_event(producer, message_type: str, **fields: object) -> None:
    producer.send_varlen(build_message(message_type, **fields))


def wait_for_ack(consumer, expected_session: str) -> None:
    deadline = time.monotonic() + HANDSHAKE_TIMEOUT_S
    while time.monotonic() < deadline:
        payload = consumer.poll_copy()
        if payload is None:
            time.sleep(POLL_DELAY_S)
            continue

        message_type, fields = parse_message(payload)
        if message_type != "ack":
            continue
        if fields.get("session") != expected_session:
            continue
        if fields.get("ok") != "1":
            raise RuntimeError("received negative ack from parent")
        if fields.get("protocol") != PROTOCOL_VERSION:
            raise RuntimeError("protocol mismatch in parent ack")
        return

    raise TimeoutError("timed out waiting for parent ack")


def run_workflow(producer) -> None:
    stages = [
        ("identity_verified", "parent ack received"),
        ("warmup", "initializing worker state"),
        ("running", "performing protected work"),
        ("finishing", "finalizing progress stream"),
    ]
    total = len(stages)
    for index, (stage, message) in enumerate(stages, start=1):
        percent = int((index * 100) / total)
        send_event(
            producer,
            "progress",
            current=index,
            total=total,
            percent=percent,
            stage=stage,
            message=message,
        )
        time.sleep(0.15)

    send_event(producer, "done")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir")
    parser.add_argument("--upstream-path", required=True)
    parser.add_argument("--downstream-path", required=True)
    parser.add_argument("--session", required=True)
    parser.add_argument("--parent-pid", required=True)
    args = parser.parse_args()

    xproc = load_xproc(args.module_dir)

    producer = xproc.Producer(make_varlen_options(xproc, args.upstream_path, False, UPSTREAM_SCHEMA_ID))
    consumer = xproc.Consumer(make_varlen_options(xproc, args.downstream_path, False, DOWNSTREAM_SCHEMA_ID))

    try:
        send_event(
            producer,
            "hello",
            session=args.session,
            pid=xproc.current_process_id(),
            parent_pid=args.parent_pid,
            protocol=PROTOCOL_VERSION,
        )
        wait_for_ack(consumer, args.session)
        run_workflow(producer)
    except Exception as exc:
        try:
            send_event(producer, "error", message=str(exc))
        except Exception:
            pass
        raise
    finally:
        consumer.close()
        producer.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
