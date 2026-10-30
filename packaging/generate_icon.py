"""Generate the RouteTec application icon using only Python's standard library."""
import pathlib
import struct
import zlib
import math


def png(size):
    data = bytearray()
    road = [(0.29, .9), (.26, .78), (.65, .67), (.7, .59), (.39, .5), (.37, .43), (.6, .34), (.59, .25), (.48, .15)]
    def distance(x, y, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        t = max(0, min(1, ((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
        return math.hypot(x-a[0]-t*dx, y-a[1]-t*dy)
    for yy in range(size):
        data.append(0)
        for xx in range(size):
            colors = []
            for oy in (.125, .375, .625, .875):
                for ox in (.125, .375, .625, .875):
                    x, y = (xx+ox)/size, (yy+oy)/size
                    color = (14, 39, 55, 255)
                    if y > .18+abs(x-.5)*1.3:
                        color = (40, 139, 119, 255)
                    if min(distance(x, y, a, b) for a, b in zip(road, road[1:])) < .045:
                        color = (245, 249, 249, 255)
                    if x < .04 or y < .04 or x > .96 or y > .96:
                        color = (14, 39, 55, 0)
                    colors.append(color)
            data.extend(round(sum(c[i] for c in colors)/len(colors)) for i in range(4))
    def chunk(name, content):
        return struct.pack('>I', len(content))+name+content+struct.pack('>I', zlib.crc32(name+content))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))+chunk(b'IDAT', zlib.compress(data))+chunk(b'IEND', b'')


frames = [(s, png(s)) for s in [16, 32, 48, 128, 256]]
header = bytearray(struct.pack('<HHH', 0, 1, len(frames)))
offset = 6 + len(frames)*16
for size, content in frames:
    header.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(content), offset))
    offset += len(content)
pathlib.Path(__file__).with_name('RouteTec.ico').write_bytes(header+b''.join(p for _, p in frames))
