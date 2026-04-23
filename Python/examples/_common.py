from __future__ import annotations

import importlib
import os
import sys
import time
from pathlib import Path
from typing import Any, Callable, Iterable


MODULE_DIR_ENV = "XPROC_PYTHON_MODULE_DIR"
REPO_ROOT = Path(__file__).resolve().parents[2]


def _dedupe_paths(paths: Iterable[Path]) -> list[Path]:
    seen: set[str] = set()
    unique: list[Path] = []
    for path in paths:
        resolved = str(path.expanduser().resolve())
        if resolved in seen:
            continue
        seen.add(resolved)
        unique.append(Path(resolved))
    return unique


def _candidate_module_dirs(explicit_module_dir: str | None) -> list[Path]:
    candidates: list[Path] = []
    if explicit_module_dir:
        candidates.append(Path(explicit_module_dir))

    env_module_dir = os.environ.get(MODULE_DIR_ENV)
    if env_module_dir:
        candidates.append(Path(env_module_dir))

    candidates.append(REPO_ROOT / "build" / "Python" / "stage")
    candidates.append(REPO_ROOT / "build-pybind" / "Python" / "stage")
    return _dedupe_paths(candidates)


def load_xproc(module_dir: str | None = None) -> Any:
    for candidate in _candidate_module_dirs(module_dir):
        if not (candidate / "xproc").exists():
            continue
        sys.path.insert(0, str(candidate))
        return importlib.import_module("xproc")

    try:
        return importlib.import_module("xproc")
    except ModuleNotFoundError as first_error:
        tried = ", ".join(str(path) for path in _candidate_module_dirs(module_dir))
        raise ModuleNotFoundError(
            "unable to import xproc. Build the staged Python package first, install it into the "
            f"active environment, or pass --module-dir / set {MODULE_DIR_ENV}. Tried: {tried}"
        ) from first_error


def module_dir_for_child(xproc_module: Any) -> str | None:
    module_file = getattr(xproc_module, "__file__", None)
    if not module_file:
        return None
    return str(Path(module_file).resolve().parents[1])


def cleanup_shm(xproc_module: Any, shm_path: str) -> None:
    try:
        xproc_module.shm_unlink(shm_path)
    except Exception:
        # Best-effort cleanup only.
        pass


def wait_for_message(
    receive: Callable[[], bytes | None],
    *,
    attempts: int = 1_000,
    delay_s: float = 0.001,
    label: str = "message",
) -> bytes:
    for _ in range(attempts):
        payload = receive()
        if payload is not None:
            return payload
        time.sleep(delay_s)

    raise TimeoutError(f"timed out waiting for {label}")


def non_negative_int(raw: str) -> int:
    value = int(raw, 10)
    if value < 0:
        raise ValueError(f"expected a non-negative integer, got {raw!r}")
    return value


def find_child_binary(
    explicit_path: str | None,
    *,
    default_target: str = "xproc_node_cpp_child_struct_writer",
) -> Path:
    candidates = (
        [Path(explicit_path)]
        if explicit_path is not None
        else [
            REPO_ROOT / "build" / "examples" / default_target,
            REPO_ROOT / "build" / "examples" / f"{default_target}.exe",
            REPO_ROOT / "build-pybind" / "examples" / default_target,
            REPO_ROOT / "build-pybind" / "examples" / f"{default_target}.exe",
        ]
    )

    for candidate in _dedupe_paths(candidates):
        if candidate.exists():
            return candidate

    tried = ", ".join(str(path.expanduser().resolve()) for path in candidates)
    raise FileNotFoundError(f"unable to locate C++ child binary. Tried: {tried}")
