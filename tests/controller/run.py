#!/usr/bin/env python3
"""Exercise the real JS and C++ controller processing without a TV or SDK."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent

js = subprocess.run(['node', '--test', str(TESTS / 'gamepad.test.js')], cwd=ROOT)
with tempfile.TemporaryDirectory(prefix='moonlight-controller-') as directory:
    directory = Path(directory)
    # Replace the two platform includes, leaving the production implementation
    # intact. The hardware, browser and packet-queue boundaries are mocked.
    source = (ROOT / 'wasm/gamepad.cpp').read_text()
    source = source.replace('#include "moonlight_wasm.hpp"', '#include "native-mocks.hpp"', 1)
    source = source.replace('#include <emscripten/emscripten.h>', '', 1)
    source += '\n' + (TESTS / 'native.test.cpp').read_text()
    translation_unit = directory / 'controller.cpp'
    translation_unit.write_text(source)
    binary = directory / 'controller-test'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-I' + str(TESTS),
               '-I' + str(ROOT / 'moonlight-common-c/src'), str(translation_unit), '-o', str(binary)]
    if os.environ.get('SANITIZE') == '1':
        command.extend(['-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    subprocess.run(command, cwd=ROOT, check=True)
    native = subprocess.run([str(binary)], cwd=ROOT)
raise SystemExit(1 if js.returncode or native.returncode else 0)
