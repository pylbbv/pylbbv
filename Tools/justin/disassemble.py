import os
import posixpath
import re

HERE = os.path.dirname(__file__)
ROOT = os.path.join(HERE, "../..")
THIS = os.path.relpath(__file__, ROOT).replace(os.path.sep, posixpath.sep)

STENCILS = os.path.relpath(os.path.join(ROOT, "Python/jit_stencils.h"))
assert os.path.isfile(STENCILS), f"Stencil file {STENCILS} doesn't exist"


stencil_h = open(STENCILS).read()
bytesmatch = r"// ([A-Z_]+)\nstatic unsigned char [^\n]+\[\] = \{([^\}]+)\};"
stencilbytes = re.findall(bytesmatch, stencil_h, re.DOTALL)
stencilbytes = {x:bytes(eval(f"[{y}]")) for x,y in stencilbytes}

from capstone import *
md = Cs(CS_ARCH_X86, CS_MODE_64)

stencilinst = [(inst, [*md.disasm(code, 0)]) for inst, code in stencilbytes.items()]

for inst, xs in stencilinst:
    print()
    print(f"{inst}: ninsts = {len(xs)}")
    for i in xs:
        print(f"    0x{i.address:05x}: {i.mnemonic} \t{i.op_str}")

ninsts = [(inst, len(x)) for inst,x in stencilinst]
ni = [x for _,x in ninsts]

print()
print("Average:", sum(ni) / len(stencilinst))
print(f"Max: '{ninsts[ni.index(max(ni))][0]}': {max(ni)}")
print(f"Min: '{ninsts[ni.index(min(ni))][0]}': {min(ni)}")
