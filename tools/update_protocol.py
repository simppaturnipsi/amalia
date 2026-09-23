"""Release publisher and reference verifier for the Amalia update protocol.

The production launcher implements the same SHA-256 + Ed25519 protocol in C++.
This module is also used by CI to test repository artifacts without Qt.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import zipfile
from base64 import b64decode, b64encode
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Callable

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey


@dataclass(frozen=True)
class Metadata:
    version: str
    channel: str
    platform: str
    file: str
    sha256: str
    signature: str
    minimum_supported_version: str = ""
    mandatory: bool = False

    @classmethod
    def load(cls, path: Path) -> "Metadata":
        return cls(**json.loads(path.read_text(encoding="utf-8")))

    def signed_payload(self) -> bytes:
        values = [self.version, self.channel, self.platform, self.file, self.sha256,
                  self.minimum_supported_version, "1" if self.mandatory else "0"]
        if any("\n" in value or "\r" in value for value in values):
            raise ValueError("metadata values may not contain newlines")
        return ("AMALIA-UPDATE-V1\n" + "\n".join(values) + "\n").encode("utf-8")


def verify_package(package: bytes, metadata: Metadata, public_key_pem: bytes) -> bool:
    if hashlib.sha256(package).hexdigest() != metadata.sha256:
        return False
    try:
        key = serialization.load_pem_public_key(public_key_pem)
        if not isinstance(key, Ed25519PublicKey):
            return False
        key.verify(b64decode(metadata.signature, validate=True), metadata.signed_payload())
        return True
    except (ValueError, InvalidSignature):
        return False


def publish(package_path: Path, repository: Path, channel: str, platform: str, version: str,
            private_key_path: Path, minimum_supported_version: str = "", mandatory: bool = False) -> Metadata:
    if channel not in {"stable", "test"} or platform not in {"windows", "ubuntu"}:
        raise ValueError("invalid channel or platform")
    package = package_path.read_bytes()
    key = serialization.load_pem_private_key(private_key_path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("an Ed25519 private key is required")
    relative = Path(channel) / platform / package_path.name
    destination = repository / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(package_path, destination)
    unsigned = Metadata(
        version=version,
        channel=channel,
        platform=platform,
        file=relative.as_posix(),
        sha256=hashlib.sha256(package).hexdigest(),
        signature="",
        minimum_supported_version=minimum_supported_version,
        mandatory=mandatory,
    )
    metadata = Metadata(**{**asdict(unsigned), "signature": b64encode(key.sign(unsigned.signed_payload())).decode("ascii")})
    metadata_path = repository / "metadata" / f"{channel}-{platform}.json"
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = metadata_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(asdict(metadata), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(metadata_path)
    return metadata


def _safe_extract(package_path: Path, destination: Path) -> None:
    with zipfile.ZipFile(package_path) as archive:
        for member in archive.infolist():
            path = PurePosixPath(member.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in member.filename:
                raise ValueError("unsafe archive path")
        archive.extractall(destination)


def reference_update(
    install_root: Path,
    repository: Path | None,
    channel: str,
    platform: str,
    public_key_pem: bytes,
    launch_probe: Callable[[Path, str], bool],
) -> tuple[str, str]:
    """CI reference for offline, verification, install and rollback behavior."""
    install_root.mkdir(parents=True, exist_ok=True)
    current_path = install_root / "current.json"
    current = json.loads(current_path.read_text()) if current_path.exists() else None
    if repository is None or not (repository / "metadata" / f"{channel}-{platform}.json").exists():
        if not current:
            raise RuntimeError("no repository and no installed version")
        executable = install_root / current["executable"]
        if not launch_probe(executable, current["version"]):
            raise RuntimeError("last working version failed")
        return current["version"], "offline"
    metadata = Metadata.load(repository / "metadata" / f"{channel}-{platform}.json")
    package_path = repository / metadata.file
    package = package_path.read_bytes()
    if not verify_package(package, metadata, public_key_pem):
        if not current:
            raise RuntimeError("untrusted update and no installed version")
        launch_probe(install_root / current["executable"], current["version"])
        return current["version"], "rejected"
    staging = install_root / "versions" / f".staging-{metadata.version}"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    _safe_extract(package_path, staging)
    executable_name = "amalia-desktop.exe" if platform == "windows" else "amalia-desktop"
    if not (staging / executable_name).is_file():
        raise RuntimeError("package has no desktop executable")
    destination = install_root / "versions" / metadata.version
    if destination.exists():
        shutil.rmtree(destination)
    staging.replace(destination)
    installed = {"version": metadata.version, "executable": f"versions/{metadata.version}/{executable_name}"}
    if current:
        (install_root / "previous.json").write_text(json.dumps(current), encoding="utf-8")
    current_path.write_text(json.dumps(installed), encoding="utf-8")
    if launch_probe(install_root / installed["executable"], metadata.version):
        return metadata.version, "updated"
    if not current:
        raise RuntimeError("new version failed and no rollback exists")
    current_path.write_text(json.dumps(current), encoding="utf-8")
    launch_probe(install_root / current["executable"], current["version"])
    return current["version"], "rolled-back"
