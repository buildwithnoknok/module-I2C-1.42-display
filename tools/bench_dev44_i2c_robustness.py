# SPDX-License-Identifier: MIT
# bench_dev44_i2c_robustness.py - DEV-44 Display I2C slave robustness, run on fw 0.7.0 (before) and 0.7.1 (after).
#   python3 pico.py run bench_dev44_i2c_robustness.py 120   (Display on GP20/21, noknok.mpy on the Pico)
#   then: python3 pico.py run bench_dev44_next.py 20   (expects err 0 as the very first read)
# Pass on fw >= 0.7.1: T1 PASS, T2a/T2b "err4 seen 0", T3 "errors 0", next-script PASS.
# T1 sticky error, T2 spurious STOP re-runs the last command, T3 status read right after a command.
# Ends with a blit chunk + i2c.deinit() and NO status read, on purpose.
import time, board, busio, digitalio
from noknok import Conductor

SDA, SCL = board.GP20, board.GP21
c = Conductor(sda=SDA, scl=SCL)
c.enumerate()
d = c.display[0]
A = d.addr if hasattr(d, "addr") else 0x08
print("fw", d.firmware_version, "addr", hex(A))
i2c = c.i2c

def wr(b):
    while not i2c.try_lock(): pass
    try: i2c.writeto(A, bytes(b))
    finally: i2c.unlock()

def st():
    r = bytearray(2)
    while not i2c.try_lock(): pass
    try: i2c.readfrom_into(A, r)
    finally: i2c.unlock()
    return r[0], r[1]

def idle():
    t = time.monotonic()
    while st()[0] & 1 and time.monotonic() - t < 1: pass

BLIT_BEGIN = [0x05, 0, 0, 8, 1, 0xFF, 0xFF, 0, 0, 0]   # 8x1 px = one data byte
BLIT_DATA = [0x06, 0xAA]

# drain whatever is pending (0.7.0: sticky, never drains)
print("start status", st(), st())

# T1: unknown command -> err 2, then a second read must be 0
wr([0x7A]); time.sleep(0.02)
a = st(); b = st()
print("T1 after bad cmd: read1 %s read2 %s -> %s" % (a, b, "PASS" if a[1] == 2 and b[1] == 0 else "FAIL"))

def blit_done():
    wr(BLIT_BEGIN); time.sleep(0.01); wr(BLIT_DATA); time.sleep(0.01)

# T2a: complete blit, then deinit + re-init the bus (what a bench script does between runs)
bad = 0; seen = set()
for n in range(20):
    st()                                   # clear (0.7.1)
    blit_done()
    i2c.deinit()
    i2c = busio.I2C(SCL, SDA, frequency=100000)
    time.sleep(0.01)
    e = st()[1]; seen.add(e)
    if e == 4: bad += 1
print("T2a deinit/re-init x20: err4 seen %d  err values %s" % (bad, sorted(seen)))

# T2b: complete blit, then bit-bang a bare STOP (no START, no address): SCL low, SDA low, SCL up, SDA up
def bare_stop():
    global i2c
    i2c.deinit()
    scl = digitalio.DigitalInOut(SCL); sda = digitalio.DigitalInOut(SDA)
    scl.switch_to_output(False, digitalio.DriveMode.OPEN_DRAIN)
    sda.switch_to_output(False, digitalio.DriveMode.OPEN_DRAIN)
    time.sleep(0.0005)
    scl.value = True; time.sleep(0.0005)      # released, pull-up takes SCL high
    sda.value = True; time.sleep(0.0005)      # SDA rises while SCL high = STOP
    scl.deinit(); sda.deinit()
    i2c = busio.I2C(SCL, SDA, frequency=100000)

bad = 0; seen = set()
for n in range(20):
    st()
    blit_done()
    bare_stop()
    time.sleep(0.01)
    e = st()[1]; seen.add(e)
    if e == 4: bad += 1
print("T2b bare STOP x20: err4 seen %d  err values %s" % (bad, sorted(seen)))

# T3: BLIT_BEGIN, status read with no delay, BLIT_DATA. A dropped BEGIN shows as err 4.
st()
bad = 0
for n in range(200):
    wr(BLIT_BEGIN); st(); idle(); wr(BLIT_DATA); idle()
    if st()[1] != 0: bad += 1
print("T3 cmd + immediate read x200: errors %d" % bad)

# visible check that drawing still works
wr([0x01, 0x00, 0x00]); idle()                                   # CLEAR black
wr([0x03, 4, 4, 0x04, 0xFF, 0xFF, 0, 0] + list(b"DEV-44")); idle()  # text, scale 2
print("final status", st())

# end like the problem scripts: a blit chunk, then deinit, no status read
blit_done()
i2c.deinit()
print("ended with blit + deinit")
