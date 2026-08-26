# -*- coding: utf-8 -*-
"""Kiểm tra sơ đồ hàn: mọi dây cắm vào cổng X-MCU phải rơi đúng lỗ của chân ghi trong nhãn,
và bảng đấu ribbon phải phủ đủ 16 chân, không trùng chân ESP32.
Chạy: python check-esp-pins.py   (trong iot/docs)"""
import io, re, sys

sys.stdout.reconfigure(encoding='utf-8')
s = io.open('perfboard-10x10-solder-guide.html', encoding='utf-8').read()

# --- bảng ribbon: [chân, chân ESP32, nối tới, ghi chú] ---
mcu = re.findall(r"\[(\d+),'([^']+)','[^']*','[^']*'\]", re.search(r"const MCU=\[(.*?)\n\];", s, re.S).group(1))
pin2esp = {int(n): p for n, p in mcu}
bad = []
if sorted(pin2esp) != list(range(1, 17)):
    bad.append("MCU: thiếu/thừa chân, có %s" % sorted(pin2esp))
sig = [p for p in pin2esp.values() if p != 'GND']
if len(sig) != len(set(sig)):
    bad.append("MCU: chân ESP32 bị lặp — %s" % sig)

# --- header trên board: cột 16 = chân lẻ, cột 17 = chân chẵn, từ hàng 12 ---
hole = {}
for cid in ('XMCUA', 'XMCUB'):
    d = re.search(r"\{id:'%s'.*?c:(\d+),r:(\d+).*?pins:\[(.*?)\]\}" % cid, s, re.S)
    c, r0 = int(d.group(1)), int(d.group(2))
    for i, nm in enumerate(d.group(3).replace("'", "").split(',')):
        pin = int(nm.split('·')[0])
        hole[(float(c), float(r0 + i))] = pin
        want = pin2esp[pin] if pin2esp[pin] != '5Vin' else '5V'
        got = nm.split('·')[1].strip()
        if got != want:
            bad.append("%s chân %d: nhãn trên hình \"%s\" ≠ bảng ribbon \"%s\"" % (cid, pin, got, want))

# --- các cổng khác (X2..X9): lỗ nào cũng phải khớp tên chân ghi trong nhãn dây ---
port = {}
for m in re.finditer(r"\{id:'([^']+)',step:\d+,kind:'(?:term|hdr)',c:(\d+),r:(\d+),n:\d+.*?pins:\[(.*?)\]", s, re.S):
    cid, c, r0, pins = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
    if cid in ('XMCUA', 'XMCUB'):
        continue
    for i, nm in enumerate(pins.replace("'", "").split(',')):
        port[(float(c), float(r0 + i))] = (cid, nm.strip())

for wid, pts, label in re.findall(r"\{id:'(w\d+)'.*?p:\[(.*?)\],t:'(.*?)'", s):
    for c, r in re.findall(r"\[([\d.]+),([\d.]+)\]", pts):
        hit = port.get((float(c), float(r)))
        if hit and hit[1] not in label:
            bad.append("%s: chạm (%s,%s) = %s.%s — nhãn nói \"%s\"" % (wid, c, r, hit[0], hit[1], label))

# --- mọi đầu dây chạm vào lỗ header phải nhắc đúng số chân ---
for wid, pts, label in re.findall(r"\{id:'(w\d+)'.*?p:\[(.*?)\],t:'(.*?)'", s):
    for c, r in re.findall(r"\[([\d.]+),([\d.]+)\]", pts):
        pin = hole.get((float(c), float(r)))
        if pin and not re.search(r"chân %d\b" % pin, label):
            bad.append("%s: chạm (%s,%s) = chân %d (%s) — nhãn nói \"%s\"" %
                       (wid, c, r, pin, pin2esp[pin], label))

# --- mỗi chân ribbon phải có đúng 1 dây hoặc 1 chân tụ trên board ---
used = set()
for pts in re.findall(r"p:\[(.*?)\],t:'", s):
    for c, r in re.findall(r"\[([\d.]+),([\d.]+)\]", pts):
        if (float(c), float(r)) in hole:
            used.add(hole[(float(c), float(r))])
for pc, pr in re.findall(r"'[^']*',(\d+),(\d+)\]", s):
    if (float(pc), float(pr)) in hole:
        used.add(hole[(float(pc), float(pr))])
nc = {n for n, e in pin2esp.items() if e == 'NC'}
missing = sorted(set(range(1, 17)) - used - nc)
wrong = sorted(used & nc)
if wrong:
    bad.append("chân NC lại có dây nối trên board: %s" % wrong)
if missing:
    bad.append("chân ribbon chưa nối gì trên board: %s" % missing)

print('\n'.join(bad) if bad else 'OK: X-MCU 16 chân + %d chân cổng ngoài — mọi dây đúng lỗ, bảng ribbon khớp hình' % len(port))
sys.exit(1 if bad else 0)
