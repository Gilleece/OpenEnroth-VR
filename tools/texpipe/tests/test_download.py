import hashlib

import pytest

from texpipe.backends.realesrgan import RELEASE_PLATFORMS, RELEASE_SHA256, verify_archive


def test_accepts_the_expected_hash(tmp_path):
    archive = tmp_path / "release.zip"
    archive.write_bytes(b"archive")
    verify_archive(archive, hashlib.sha256(b"archive").hexdigest())


def test_rejects_any_other_hash(tmp_path):
    archive = tmp_path / "release.zip"
    archive.write_bytes(b"tampered")
    with pytest.raises(ValueError):
        verify_archive(archive, hashlib.sha256(b"archive").hexdigest())


def test_every_platform_has_a_pinned_hash():
    assert set(RELEASE_SHA256) == set(RELEASE_PLATFORMS.values())
    assert all(len(digest) == 64 for digest in RELEASE_SHA256.values())
