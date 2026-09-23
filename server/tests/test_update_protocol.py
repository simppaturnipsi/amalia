import json
import sys
import zipfile
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from update_protocol import publish, reference_update


def keys(tmp_path):
    private = Ed25519PrivateKey.generate()
    private_path = tmp_path / "private.pem"
    private_path.write_bytes(private.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()
    ))
    public = private.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    return private_path, public


def package(tmp_path, version="2.0.0"):
    path = tmp_path / f"amalia-{version}.zip"
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("amalia-desktop", f"version={version}")
    return path


def existing_install(root):
    executable = root / "versions/1.0.0/amalia-desktop"
    executable.parent.mkdir(parents=True)
    executable.write_text("version=1.0.0")
    (root / "current.json").write_text(json.dumps({
        "version": "1.0.0", "executable": "versions/1.0.0/amalia-desktop"
    }))


def test_update_server_down_launches_last_working_version(tmp_path):
    root = tmp_path / "install"
    existing_install(root)
    calls = []
    version, action = reference_update(root, None, "stable", "ubuntu", b"unused",
                                       lambda path, value: calls.append((path, value)) or True)
    assert (version, action) == ("1.0.0", "offline")
    assert calls[0][0].exists()


def test_corrupted_package_is_not_installed(tmp_path):
    root, repository = tmp_path / "install", tmp_path / "repository"
    existing_install(root)
    private, public = keys(tmp_path)
    metadata = publish(package(tmp_path), repository, "stable", "ubuntu", "2.0.0", private)
    (repository / metadata.file).write_bytes((repository / metadata.file).read_bytes() + b"corruption")
    version, action = reference_update(root, repository, "stable", "ubuntu", public, lambda *_: True)
    assert (version, action) == ("1.0.0", "rejected")


def test_invalid_signature_is_not_installed(tmp_path):
    root, repository = tmp_path / "install", tmp_path / "repository"
    existing_install(root)
    private, public = keys(tmp_path)
    publish(package(tmp_path), repository, "stable", "ubuntu", "2.0.0", private)
    path = repository / "metadata/stable-ubuntu.json"
    document = json.loads(path.read_text())
    document["signature"] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="
    path.write_text(json.dumps(document))
    assert reference_update(root, repository, "stable", "ubuntu", public, lambda *_: True)[1] == "rejected"


def test_signed_package_cannot_be_relabelled_as_newer_version(tmp_path):
    root, repository = tmp_path / "install", tmp_path / "repository"
    existing_install(root)
    private, public = keys(tmp_path)
    publish(package(tmp_path), repository, "stable", "ubuntu", "2.0.0", private)
    path = repository / "metadata/stable-ubuntu.json"
    document = json.loads(path.read_text())
    document["version"] = "99.0.0"
    path.write_text(json.dumps(document))
    assert reference_update(root, repository, "stable", "ubuntu", public, lambda *_: True)[1] == "rejected"


def test_failed_new_version_rolls_back(tmp_path):
    root, repository = tmp_path / "install", tmp_path / "repository"
    existing_install(root)
    private, public = keys(tmp_path)
    publish(package(tmp_path), repository, "stable", "ubuntu", "2.0.0", private)
    calls = []
    def probe(_path, version):
        calls.append(version)
        return version == "1.0.0"
    version, action = reference_update(root, repository, "stable", "ubuntu", public, probe)
    assert (version, action) == ("1.0.0", "rolled-back")
    assert calls == ["2.0.0", "1.0.0"]
    assert json.loads((root / "current.json").read_text())["version"] == "1.0.0"


def test_valid_update_becomes_current(tmp_path):
    root, repository = tmp_path / "install", tmp_path / "repository"
    existing_install(root)
    private, public = keys(tmp_path)
    publish(package(tmp_path), repository, "test", "ubuntu", "2.0.0", private)
    assert reference_update(root, repository, "test", "ubuntu", public, lambda *_: True) == ("2.0.0", "updated")
