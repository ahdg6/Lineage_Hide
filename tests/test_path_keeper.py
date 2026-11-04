"""Exercise the real keeper/Unix socket and VFS references, without root/SUSFS."""
import os
import signal
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'module/src/main/cpp'


class PathKeeperTests(unittest.TestCase):
    def test_lifecycle_and_status(self):
        with tempfile.TemporaryDirectory(prefix='lh-keeper-') as directory:
            base = Path(directory)
            (base / 'runtime').mkdir(mode=0o700)
            target = base / 'test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                '-DLH_HOST_TEST', f'-DLH_STATE_DIR="{base}/state"', f'-DLH_RUNTIME_DIR="{base}/runtime"',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-I', str(CPP),
                str(ROOT / 'tests/path_keeper_test.cpp'),
                *[str(CPP / name) for name in ['config.cpp', 'susfs_backend.cpp', 'path_keeper.cpp']],
                '-o', str(target)], check=True)
            try:
                subprocess.run([str(target)], check=True, timeout=20)
            finally:
                # A failed assertion/timeout may orphan the intentionally detached
                # keeper. Only reap processes executing this unique fixture binary.
                for proc in Path('/proc').glob('[0-9]*'):
                    try:
                        if (proc / 'exe').readlink() == target:
                            os.kill(int(proc.name), signal.SIGKILL)
                    except (FileNotFoundError, ProcessLookupError, PermissionError):
                        pass
