#!/bin/bash
set -e
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CLANG=$(xcrun --find clang)
echo "SDK: $SDK"
echo "CLANG: $CLANG"

$CLANG -target arm64-apple-ios16.0 -dynamiclib -O2 \
  -isysroot "$SDK" \
  -I libkfd -I libkfd/puaf -I libkfd/krkw \
  -Wl,-undefined,dynamic_lookup \
  entry1_kfd.c -o entry1_kfd.dylib -lobjc

# 校验 dyld 元数据
python3 - << 'PYEOF'
import struct
d = open('entry1_kfd.dylib','rb').read()
ncmds = struct.unpack_from('<I', d, 16)[0]
off = 32
for i in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd in (0x22, 0x80000022):
        rb, rs, bo, bs, lo, ls = struct.unpack_from('<IIIIII', d, off + 8)
        print('DYLD_INFO rebase=%d bind=%d lazy=%d' % (rs, bs, ls))
    if cmd == 0xB:
        ds = struct.unpack_from('<12I', d, off + 8)
        print('DYSYMTAB nindirectsyms=%d' % ds[7])
    off += size
PYEOF

# arm64e 头 patch
python3 - << 'PYEOF'
import struct
P='entry1_kfd.dylib'
d=bytearray(open(P,'rb').read())
struct.pack_into('<i', d, 8, 2)   # cpusubtype = arm64e
open(P,'wb').write(d)
print('patched arm64e')
PYEOF

ls -la entry1_kfd.dylib
