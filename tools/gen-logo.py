#!/usr/bin/env python3
# plugin/art/slapworx-wordmark.png -> plugin/LogoData.hpp (the slapworx wordmark drawn bottom right)
import os
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
data = open(os.path.join(root, 'plugin/art/slapworx-wordmark.png'), 'rb').read()
rows = [','.join('0x%02x' % b for b in data[i:i + 20]) for i in range(0, len(data), 20)]
with open(os.path.join(root, 'plugin/LogoData.hpp'), 'w') as f:
    f.write('#pragma once\n// The slapworx wordmark (PNG, 480 px wide). Made by tools/gen-logo.py from plugin/art/slapworx-wordmark.png.\n\n')
    f.write('static const unsigned char kLogoPng[] = {\n' + ',\n'.join(rows) + '\n};\n')
    f.write('static const unsigned int kLogoPngSize = %d;\n' % len(data))
print('LogoData.hpp:', len(data), 'bytes')
