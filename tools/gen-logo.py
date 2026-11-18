#!/usr/bin/env python3
# plugin/art/slapworx-wordmark.png -> plugin/LogoData.hpp (the slapworx wordmark drawn bottom right)
import io, os
from PIL import Image
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
# drawn 120 pt wide: keep 240 px (sharp at 2x, an exact 2:1 average at 1x) and no mipmaps, because
# mipmap generation isn't reliable on every OpenGL driver (it gave a black box on macOS)
im = Image.open(os.path.join(root, 'plugin/art/slapworx-wordmark.png')).convert('RGBA')
im = im.resize((240, round(im.height * 240 / im.width)), Image.LANCZOS)
buf = io.BytesIO(); im.save(buf, 'PNG', optimize=True); data = buf.getvalue()
rows = [','.join('0x%02x' % b for b in data[i:i + 20]) for i in range(0, len(data), 20)]
with open(os.path.join(root, 'plugin/LogoData.hpp'), 'w') as f:
    f.write('#pragma once\n// The slapworx wordmark (PNG, 240 px wide). Made by tools/gen-logo.py from plugin/art/slapworx-wordmark.png.\n\n')
    f.write('static const unsigned char kLogoPng[] = {\n' + ',\n'.join(rows) + '\n};\n')
    f.write('static const unsigned int kLogoPngSize = %d;\n' % len(data))
print('LogoData.hpp:', len(data), 'bytes')
