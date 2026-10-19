"""Inflate a (possibly truncated) gzip .psp2dmp and zero-pad the ELF core to its declared size,
so external/vita-parse-core can read a crash dump the Vita did not finish writing.
  python tools/vita/pad_core.py <in.psp2dmp[.tmp]> <out.psp2dmp>
"""
import gzip
import struct
import sys
import zlib

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, "rb").read()
d = zlib.decompressobj(16 + zlib.MAX_WBITS)
data = d.decompress(raw)
phoff, = struct.unpack_from("<I", data, 0x1C)
phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
end = 0
for i in range(phnum):
    off = phoff + i * phentsize
    p_offset, = struct.unpack_from("<I", data, off + 4)
    p_filesz, = struct.unpack_from("<I", data, off + 16)
    end = max(end, p_offset + p_filesz)
print(f"inflated {len(data)}, declared {end}")
data += b"\0" * max(0, end - len(data))
with gzip.open(dst, "wb") as f:
    f.write(data)
