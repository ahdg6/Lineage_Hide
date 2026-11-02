"""Run the actual SUSFS command runner without access to a phone or ksud."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'module/src/main/cpp'


class RunnerTests(unittest.TestCase):
    def test_exit_output_arguments_and_deadlines(self):
        with tempfile.TemporaryDirectory(prefix='lh-runner-') as directory:
            target = Path(directory) / 'runner_test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                '-DLH_HOST_TEST', '-DLH_RUNNER_TEST', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-g', '-I', str(CPP), str(ROOT / 'tests/runner_test.cpp'),
                str(CPP / 'config.cpp'), '-o', str(target)], check=True)
            subprocess.run([str(target)], check=True, timeout=15)
