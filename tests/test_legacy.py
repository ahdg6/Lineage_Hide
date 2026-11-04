"""Exercise optional boot script control flow with all device writes isolated."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LegacyTests(unittest.TestCase):
    def test_normal_boot_properties_and_repeat_guard(self):
        with tempfile.TemporaryDirectory(prefix='lh-legacy-') as name:
            base = Path(name)
            for directory in ['bin', 'system/bin', 'data/system', 'dev']:
                (base / directory).mkdir(parents=True, exist_ok=True)
            (base / 'system/bin/service').write_bytes(b'\x7fELFfixture')
            (base / 'system/bin/pm').write_text('original pm')
            log = base / 'commands.log'
            marker = base / 'injected'
            def tool(name, source):
                p = base / 'bin' / name
                p.write_text('#!/bin/sh\n' + source)
                p.chmod(0o755)
            tool('resetprop', '''case "$1" in
-n|--delete) printf '%s\\n' "$@" >> "$LH_TEST_LOG" ;;
*) printf 'normal\\n' ;;
esac
''')
            tool('nsenter', 'printf "mount-call\\n" >> "$LH_TEST_LOG"\n')
            tool('chcon', 'exit 0\n')
            tool('chown', 'exit 0\n')
            tool('stat', '''case "$2" in
%a) echo 755 ;;
%u:%g) echo 0:0 ;;
%C) echo u:object_r:system_file:s0 ;;
*) exit 1 ;;
esac
''')
            # A property containing shell syntax must stay literal data.
            props = f'[ro.test]: [lineage_$(touch {marker})]\n[ro.lineage.version]: [23]\n'
            (base / 'props').write_text(props)
            tool('getprop', f"cat '{base / 'props'}'\n")
            source = (ROOT / 'module/scripts/legacy-service.sh').read_text()
            for path in ['/system/bin', '/data/system', '/dev/lineage_hide']:
                source = source.replace(path, str(base) + path)
            script = base / 'legacy.sh'
            script.write_text(source)
            env = {**os.environ, 'PATH': str(base / 'bin') + ':' + os.environ['PATH'], 'LH_TEST_LOG': str(log)}
            p = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            commands = log.read_text()
            self.assertEqual(commands.count('mount-call'), 2)
            self.assertIn('ro.boot.vbmeta.device_state\nlocked', commands)
            self.assertIn('ro.config.ringtone\nunknown', commands)
            self.assertIn('--delete\nro.lineage.version', commands)
            self.assertIn(f'$(touch {marker})', commands)
            self.assertFalse(marker.exists())
            again = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(log.read_text(), commands)

if __name__ == '__main__':
    unittest.main()
