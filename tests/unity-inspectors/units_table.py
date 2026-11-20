# Writes Assets/Data/units-float32.txt: what the firmware's param_text writes for every value of the seven parameters
# that go through vd_exp2 - computed here in IEEE float32, step by step as the C code does (sfx-forge.c param_text,
# time_text; the SDK's vd_exp2 and vg_ftoa). SfxUnitsTests reads it. Usage: python units_table.py
import os
import numpy as np

F = np.float32


def exp2(x):
    x = F(x)
    if x < F(-126.0): return F(0.0)
    if x > F(126.0): x = F(126.0)
    i = int(x)
    if x < 0 and x != F(i): i -= 1
    f = F(x - F(i))
    p = F(F(1.0) + F(f * F(F(0.6931472) + F(f * F(F(0.2402265) + F(f * F(F(0.0555041) + F(f * F(F(0.0096181) + F(f * F(0.0013334)))))))))))
    return F(p * F(2.0 ** i))


def itoa(i): return str(int(i))


def ftoa1(v):
    iv = int(F(F(F(v) * F(10)) + F(0.5)))
    return itoa(iv // 10) + '.' + itoa(iv % 10)


def time_text(s):
    s = F(s)
    if s < F(0.9995): return itoa(int(F(F(s * F(1000.0)) + F(0.5)))) + 'ms'
    return ftoa1(s) + 's'


def curve(a, u, octaves): return F(F(a) * exp2(F(u * F(octaves))))


def text(k, v):
    u = F(F(v) / F(127.0))
    if k in ('BaseFreq', 'FilterCutoff'):
        hz = curve(20.0, u, 9.5) if k == 'BaseFreq' else curve(40.0, u, 8.6)
        return ftoa1(F(hz / F(1000.0))) + 'kHz' if hz >= F(1000.0) else itoa(int(hz)) + 'Hz'
    if k == 'Attack': return time_text(curve(0.0015, u, 11.4))
    if k == 'AmpDecay': return time_text(curve(0.010, u, 8.6))
    if k == 'Release': return time_text(curve(0.005, u, 9.6))
    if k in ('PitchEnvDecay', 'FilterEnvDecay'): return time_text(curve(0.005, u, 9.0))
    if k == 'LfoRate':
        hz = curve(0.1, u, 8.2)
        return (ftoa1(hz) if hz < F(10.0) else itoa(int(hz))) + 'Hz'
    raise ValueError(k)


here = os.path.dirname(os.path.abspath(__file__))
rows = []
for k in ('BaseFreq', 'FilterCutoff', 'Attack', 'AmpDecay', 'Release', 'PitchEnvDecay', 'FilterEnvDecay', 'LfoRate'):
    for v in range(128):
        rows.append('%s %d %s' % (k, v, text(k, v)))
with open(os.path.join(here, 'Assets', 'Data', 'units-float32.txt'), 'w', newline='\n') as out:
    out.write('\n'.join(rows) + '\n')
print(len(rows), 'rows;', [r for r in rows if r.startswith('BaseFreq 26 ') or r.startswith('Attack 100 ') or r.startswith('BaseFreq 30 ') or r.startswith('LfoRate 87 ')])
