from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import site
import sysconfig
from pathlib import Path
from urllib.parse import unquote, urlparse
from urllib.request import url2pathname

if not __debug__:
    raise RuntimeError("smoke tests must not run with python -O")


def _installed_library_dirs() -> list[Path]:
    candidates: list[Path] = []
    for key in ("purelib", "platlib"):
        path = sysconfig.get_path(key)
        if path is None:
            continue
        resolved = Path(path).resolve()
        if resolved not in candidates:
            candidates.append(resolved)
    try:
        user_site = site.getusersitepackages()
    except AttributeError:
        user_site = None
    if user_site is not None:
        resolved = Path(user_site).resolve()
        if resolved not in candidates:
            candidates.append(resolved)
    get_site_packages = getattr(site, "getsitepackages", None)
    if get_site_packages is not None:
        for path in get_site_packages():
            resolved = Path(path).resolve()
            if resolved not in candidates:
                candidates.append(resolved)
    return candidates


def _load_smoke_helper():
    repo_root = Path(__file__).resolve().parents[1]
    smoke_test_path = repo_root / "Python" / "tests" / "smoke_test.py"
    spec = importlib.util.spec_from_file_location("xproc_stage_smoke", smoke_test_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load smoke helper from {smoke_test_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _expected_wheel_version(expected_wheel: Path) -> str:
    filename = expected_wheel.name
    if not filename.endswith(".whl"):
        raise AssertionError(f"expected a wheel path, got {expected_wheel}")
    stem = filename[:-4]
    parts = stem.split("-")
    if len(parts) < 5:
        raise AssertionError(f"unexpected wheel filename format: {filename}")
    return parts[1]


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _direct_url_path(record: dict[str, object]) -> Path | None:
    url = record.get("url")
    if not isinstance(url, str):
        return None
    parsed = urlparse(url)
    if parsed.scheme != "file":
        return None
    raw_path = url2pathname(unquote(parsed.path))
    if parsed.netloc:
        raw_path = f"//{parsed.netloc}{raw_path}"
    return Path(raw_path).resolve()


def _verify_direct_url(
    distribution: importlib.metadata.Distribution, expected_wheel: Path
) -> None:
    direct_url_text = distribution.read_text("direct_url.json")
    assert direct_url_text is not None, (
        "installed distribution did not provide direct_url.json in provenance mode"
    )

    direct_url = json.loads(direct_url_text)
    assert isinstance(direct_url, dict), "direct_url.json did not contain an object"

    recorded_path = _direct_url_path(direct_url)
    assert recorded_path is not None, (
        "direct_url.json did not record a usable file:// wheel path"
    )
    assert recorded_path == expected_wheel, (
        f"installed direct_url.json recorded {recorded_path}, expected {expected_wheel}"
    )

    archive_info = direct_url.get("archive_info")
    if archive_info is None:
        return
    assert isinstance(archive_info, dict), "direct_url.json archive_info was not an object"

    recorded_hash = archive_info.get("hash")
    hashes = archive_info.get("hashes")
    expected_sha256 = None

    if isinstance(recorded_hash, str):
        algorithm, _, value = recorded_hash.partition("=")
        assert algorithm == "sha256" and value, (
            f"unsupported direct_url.json hash format: {recorded_hash}"
        )
        expected_sha256 = value
    elif recorded_hash is not None:
        raise AssertionError(
            f"direct_url.json archive_info.hash had unsupported type: {type(recorded_hash).__name__}"
        )
    elif hashes is not None:
        assert isinstance(hashes, dict), "direct_url.json archive_info.hashes was not an object"
        sha256_value = hashes.get("sha256")
        assert isinstance(sha256_value, str) and sha256_value, (
            "direct_url.json archive_info.hashes['sha256'] was missing or invalid"
        )
        expected_sha256 = sha256_value
    else:
        return

    actual_hash = _file_sha256(expected_wheel)
    assert actual_hash == expected_sha256, (
        f"wheel sha256 {actual_hash} did not match recorded {expected_sha256} "
        "in direct_url.json"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-wheel")
    args = parser.parse_args()

    expected_wheel = None
    expected_version = None
    if args.expected_wheel is not None:
        expected_wheel = Path(args.expected_wheel).resolve()
        assert expected_wheel.is_file(), f"expected wheel file at {expected_wheel}"
        expected_version = _expected_wheel_version(expected_wheel)

    smoke_test = _load_smoke_helper()
    import xproc

    package_file = Path(xproc.__file__).resolve()
    library_dirs = _installed_library_dirs()
    assert any(package_file.is_relative_to(path) for path in library_dirs), (
        f"expected installed wheel import under one of {library_dirs}, got {package_file}"
    )

    distribution_files = importlib.metadata.files("xproc")
    assert distribution_files is not None
    assert all(Path(entry).name != "xproc.pth" for entry in distribution_files), (
        "installed wheel should not include xproc.pth"
    )
    if expected_version is not None and expected_wheel is not None:
        distribution = importlib.metadata.distribution("xproc")
        _verify_direct_url(distribution, expected_wheel)
        installed_version = importlib.metadata.version("xproc")
        assert installed_version == expected_version, (
            f"installed xproc version {installed_version} did not match {expected_version} "
            f"from {expected_wheel.name}"
        )
        expected_dist_info_dir = f"xproc-{expected_version}.dist-info"
        assert any(Path(entry).parts[0] == expected_dist_info_dir for entry in distribution_files), (
            f"installed wheel metadata did not match {expected_dist_info_dir}"
        )

    smoke_test.run_smoke_checks(xproc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
