"""Check startup retry/publication with isolated Android command substitutes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BootTests(unittest.TestCase):
    def test_package_snapshot_and_failed_startup(self):
        for ready in [True, False]:
            with self.subTest(ready=ready), tempfile.TemporaryDirectory(prefix='lh-boot-') as directory:
                base = Path(directory)
                (base / 'bin').mkdir()
                runtime = base / 'runtime'

                def tool(name, source):
                    path = base / 'bin' / name
                    path.write_text('#!/bin/sh\n' + source)
                    path.chmod(0o755)

                tool('getprop', 'exit 0\n')
                tool('sleep', 'exit 0\n')
                # Preserve the timeout contract but execute fast fixture commands.
                tool('toybox', '''[ "$1 $2 $3 $4" = "timeout -s KILL 2" ] || exit 2
shift 4
exec "$@"
''')
                tool('cmd', '''# Binder's remote system_server must receive a pipe, not a private file.
[ -p /proc/self/fd/1 ] || exit 2
n=$(cat "$LH_TEST_BASE/attempts" 2>/dev/null || echo 0)
n=$((n+1)); echo "$n" > "$LH_TEST_BASE/attempts"
if [ "$LH_TEST_READY" = 1 ] && [ "$n" -ge 2 ]; then
  printf 'package:com.android.settings\npackage:android\npackage:android\n'
else
  echo 'package:com.android.settings'
fi
''')
                tool('configctl', '''echo "$*" >> "$LH_TEST_BASE/control.log"
case "$1" in
init|apply-paths) exit 0 ;;
legacy-boot) exit 1 ;;
*) exit 2 ;;
esac
''')
                source = (ROOT / 'module/service.sh').read_text()
                source = source.replace('/dev/lineage_hide', str(runtime)).replace('/system/bin/', str(base / 'bin') + '/')
                script = base / 'service.sh'
                script.write_text(source)
                env = {**os.environ, 'PATH': str(base / 'bin') + ':' + os.environ['PATH'],
                       'LH_TEST_BASE': str(base), 'LH_TEST_READY': str(int(ready))}
                subprocess.run(['sh', str(script)], env=env, check=True, timeout=10)
                self.assertEqual(int((base / 'attempts').read_text()), 2 if ready else 60)
                self.assertEqual((runtime / 'system_packages.txt').exists(), ready)
                if ready:
                    self.assertEqual((runtime / 'system_packages.txt').read_text(), 'android\ncom.android.settings\n')
                    self.assertEqual((runtime / 'system_packages.txt').stat().st_mode & 0o777, 0o600)
                self.assertFalse((runtime / 'packages.raw').exists())
                self.assertFalse((runtime / 'packages.tmp').exists())
                self.assertEqual((base / 'control.log').read_text(), 'init lineage\napply-paths\nlegacy-boot\n')
                self.assertEqual((runtime / 'legacy_boot').read_text(), 'disabled\n')
