"""Run hook regressions against a small fake JNI/Parcel implementation.

Use the real NDK JNI declarations; no Android device/Java VM is required.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'module/src/main/cpp'

class JniTests(unittest.TestCase):
    def test_binder_and_partial_resource_hooks(self):
        candidates = [Path(os.environ.get('JNI_HEADER', '/nonexistent/jni.h'))]
        sdk = os.environ.get('ANDROID_SDK_ROOT', os.environ.get('ANDROID_HOME', ''))
        if sdk:
            candidates += list(Path(sdk).glob('ndk/*/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h'))
        header = next((p for p in candidates if p.is_file()), None)
        self.assertIsNotNone(header, 'Set JNI_HEADER to the NDK sysroot jni.h, or set ANDROID_SDK_ROOT with an installed NDK')
        with tempfile.TemporaryDirectory(prefix='lh-jni-') as directory:
            tmp = Path(directory)
            shutil.copyfile(header, tmp / 'jni.h')
            target = tmp / 'jni_test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                '-Wno-write-strings', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-pthread',
                '-I', str(tmp), '-I', str(CPP), str(ROOT / 'tests/jni_test.cpp'),
                *[str(CPP / f) for f in ['config.cpp', 'service_filter.cpp', 'feature_filter.cpp', 'broadcast_filter.cpp']],
                '-o', str(target)], check=True)
            subprocess.run([str(target)], check=True)
