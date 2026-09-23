"""Shared helpers for the playtest harness's tests.

Here for one reason: the fake engine is a Python script with a shebang, which
POSIX executes directly and Windows cannot -- CreateProcess does not read
shebangs, so the runner gets exit 127 and the failure looks like a broken
engine rather than a test that cannot launch one. Windows therefore needs a
shim, and two suites hand the runner a fake binary, so the shim lives in one
place instead of drifting between them.
"""

import atexit
import os
import shutil
import stat
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TESTDATA = HERE / "testdata"
FAKE_SCRIPT = TESTDATA / "fake-ai-arena"


def _write_shim(path: Path) -> Path:
    """
    A .cmd that runs the fake engine under the interpreter running the tests.

    Whichever that is, so this works in MSYS, in a virtualenv and in a plain
    install alike, rather than depending on what `python` happens to mean on
    the PATH.
    """
    body = ["@echo off", '"{}" "{}" %*'.format(sys.executable, FAKE_SCRIPT), ""]
    path.write_text("\r\n".join(body), encoding="ascii")
    return path


def fake_binary() -> Path:
    """The fake engine, as a path the runner can launch."""
    if os.name != "nt":
        return FAKE_SCRIPT

    shim_dir = tempfile.mkdtemp(prefix="rwe-playtest-shim-")
    atexit.register(shutil.rmtree, shim_dir, ignore_errors=True)
    return _write_shim(Path(shim_dir) / "fake-ai-arena.cmd")


def copy_fake_binary(directory, mtime=None) -> Path:
    """
    A launchable copy of the fake engine in `directory`, with `mtime` set.

    A copy rather than the original because the staleness check reads the
    binary's modification time, and a test that wants to be stale has to
    choose it.
    """
    directory = Path(directory)
    if os.name == "nt":
        binary = _write_shim(directory / "fake-ai-arena.cmd")
    else:
        binary = directory / "fake-ai-arena"
        shutil.copyfile(FAKE_SCRIPT, binary)
        binary.chmod(binary.stat().st_mode | stat.S_IEXEC)

    if mtime is not None:
        os.utime(binary, (mtime, mtime))

    return binary
