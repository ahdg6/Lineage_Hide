"""Run portable tests with the same parser/storage/transport as Android."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'module/src/main/cpp'

class ConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='lineage-hide-test-')
        cls.path = Path(cls.tmp.name)
        (cls.path / 'runtime').mkdir()
        compiler = os.environ.get('CXX', 'c++')
        common = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread',
                  '-DLH_HOST_TEST', f'-DLH_STATE_DIR="{cls.path}/state"',
                  f'-DLH_RUNTIME_DIR="{cls.path}/runtime"', '-I', str(CPP)]
        cls.ctl = cls.path / 'configctl'
        subprocess.run(common + [str(CPP / name) for name in ['config.cpp', 'configctl.cpp', 'susfs_backend.cpp']]
                       + ['-o', str(cls.ctl)], check=True)
        cls.native = cls.path / 'config_test'
        subprocess.run(common + ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
            str(ROOT / 'tests/config_test.cpp')] + [str(CPP / name) for name in
            ['config.cpp', 'config_transport.cpp', 'service_filter.cpp', 'feature_filter.cpp', 'broadcast_filter.cpp', 'susfs_backend.cpp']]
            + ['-o', str(cls.native)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def call(self, *args, ok=True):
        p = subprocess.run([str(self.ctl), *args], text=True, capture_output=True)
        self.assertEqual(p.returncode == 0, ok, p.stderr)
        return p.stdout

    def test_01_native_and_transport(self):
        subprocess.run([str(self.native)], check=True)

    def test_02_atomic_save_and_upgrade(self):
        base = self.call('preset', 'evox')
        self.call('set', base)
        saved = self.call('get')
        self.assertEqual(saved, base)
        self.assertEqual(self.call('init', 'lineage'), base)
        self.assertEqual((self.path / 'state/config.conf').stat().st_mode & 0o777, 0o600)
        for bad in [base + 'enabled=0\n', base + 'package=bad;touch /tmp/no\n', base + 'schema=2\n']:
            self.call('set', bad, ok=False)
            self.assertEqual(self.call('get'), saved)
        self.call('legacy-boot', ok=False)
        self.call('set', base.replace('legacy_boot=0', 'legacy_boot=1'))
        self.call('legacy-boot')
        self.call('set', base.replace('legacy_boot=0', 'legacy_boot=1').replace('enabled=1', 'enabled=0'))
        self.call('legacy-boot', ok=False)

    def test_025_native_paths_fail_closed_on_host(self):
        base = self.call('preset', 'evox')
        self.call('set', base)
        preview = self.call('path-plan', base)
        self.assertIn('native_paths_enabled=1\n', preview)
        self.call('apply-paths', ok=False)
        self.assertIn('paths_status=unsupported\n', self.call('status'))
        self.call('set', base.replace('native_paths=1', 'native_paths=0'))
        self.call('apply-paths')
        self.assertIn('paths_status=disabled\n', self.call('status'))
        self.assertIn('reboot_required', (self.path / 'runtime/paths.log').read_text())

    def test_03_bounds_and_corruption(self):
        base = self.call('preset', 'lineage')
        huge = base + ''.join(f'package=org.example.app{i}\n' for i in range(513))
        self.call('validate', huge, ok=False)
        self.call('validate', base + 'feature=' + 'x' * 256 + '\n', ok=False)
        self.call('validate', base + 'resource_id=FF\n', ok=False)
        (self.path / 'state/config.conf').write_text('schema=1\n')
        self.call('get', ok=False)
        self.call('init', 'evox', ok=False)
        self.assertEqual((self.path / 'state/config.conf').read_text(), 'schema=1\n')
        (self.path / 'state/config.conf').unlink()
        (self.path / 'state/config.conf').symlink_to(self.path / 'outside')
        self.call('get', ok=False)
        self.call('init', 'lineage', ok=False)
        self.assertFalse((self.path / 'outside').exists())

if __name__ == '__main__':
    unittest.main()
