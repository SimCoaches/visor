#!/usr/bin/env python3

import builtins
import os, subprocess
from sys import platform

dir = os.path.abspath('.')
is_rel = dir.endswith('_rel')
build_type = 'RelWithDebInfo' if is_rel else 'Debug'

commands = [
    ['conan', 'install', '..', '-s', 'build_type=%s' % build_type, '--build=missing'],
    ['cmake', '..', '-DCMAKE_BUILD_TYPE=%s' % build_type],
    ['cmake', '--build', '.', '--config', build_type]
]

if platform == 'linux':
    commands[1].insert(1, '-G')
    commands[1].insert(2, 'Ninja')

for command_parts in commands:
    print('$', command_parts)
    try:
        return_code = subprocess.call(command_parts, shell=True)
        if return_code != 0:
            quit()
    except subprocess.CalledProcessError as grepexc:
        quit()