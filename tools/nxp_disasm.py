#!/usr/bin/env python3
"""Recursive-descent Thumb disassembler for the NXP LPC11U67 flat image.

Follows the vector table, literal-pool code pointers and the Keil switch8
helper (app only, at 0x87ec). Annotates literal loads with peripheral names.
Needs capstone (pip install capstone in a venv).

usage: nxp_disasm.py nxp_lpc11u67.bin VECTOR_BASE OUT.txt
       VECTOR_BASE is 0 for the BootCode, 0x2000 for the application.
"""
import sys, struct, collections
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
from capstone.arm import *
img=open(sys.argv[1],'rb').read(); base=int(sys.argv[2],0); out=sys.argv[3]
lo,hi=(0,0x1f00) if base==0 else (0x2000,0x9a00)
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS); md.detail=True
PER={0x40000000:'I2C0',0x40004000:'WWDT',0x40008000:'USART0',0x4000c000:'CT16B0',0x40010000:'CT16B1',0x40014000:'CT32B0',0x40018000:'CT32B1',0x4001c000:'ADC',0x40020000:'I2C1',0x40024000:'RTC',0x40038000:'PMU',0x4003c000:'FLASHCTRL',0x40040000:'SSP0',0x40044000:'IOCON',0x40048000:'SYSCON',0x4004c000:'USART4',0x40058000:'SSP1',0x4005c000:'GINT0',0x40060000:'GINT1',0x4006c000:'USART1',0x40070000:'USART2',0x40074000:'USART3',0x40080000:'USB',0x50000000:'DMA',0x5000c000:'SCT0',0x5000e000:'SCT1',0xa0000000:'GPIO',0xa0004000:'PINT',0xe000e000:'SCS'}
def pname(w):
    b=w&~0x3fff
    if b in PER: return '%s+0x%x'%(PER[b],w-b)
    if 0x10000000<=w<0x10009000: return 'RAM'
    if lo<=(w&~1)<hi and w&1: return 'code'
    if lo<=w<hi: return 'rom'
    return ''
insn={}; funcs=set(); xref=collections.defaultdict(set); lits={}
work=[]; sw={}; swt=collections.defaultdict(list)
vec=struct.unpack_from('<48I',img,base)
for v in vec[1:]:
    if v&1 and lo<=v<hi: funcs.add(v&~1); work.append(v&~1)
# also scan for code pointers in literal words later
def walk(a):
    while lo<=a<hi and a not in insn:
        code=img[a:a+4]
        l=list(md.disasm(code,a,1))
        if not l: return
        i=l[0]; insn[a]=i
        m=i.mnemonic; ops=i.operands
        if m=='bl':
            t=ops[0].imm; funcs.add(t); xref[t].add(a); work.append(t)
            if t==0x87ec and base==0x2000:
                tb=a+4; n=img[tb]
                sw[tb]=n
                for k in range(n+1):
                    tt=tb+2*img[tb+1+k]; xref[tt].add(a); work.append(tt); swt[tt].append('%x:case%s'%(a,k if k<n else 'default'))
                return
        elif m in('b','b.w') :
            t=ops[0].imm; xref[t].add(a); work.append(t); return
        elif m.startswith('b') and m not in('bx','blx','bics','bkpt','bic') and ops and ops[0].type==ARM_OP_IMM:
            t=ops[0].imm; xref[t].add(a); work.append(t)
        elif m in('cbz','cbnz'):
            work.append(ops[1].imm)
        elif m=='bx': return
        elif m=='pop' and any(o.reg==ARM_REG_PC for o in ops): return
        elif m=='mov' and ops[0].reg==ARM_REG_PC: return
        if m in('ldr',) and len(ops)==2 and ops[1].type==ARM_OP_MEM and ops[1].mem.base==ARM_REG_PC:
            la=((a+4)&~3)+ops[1].mem.disp
            w=struct.unpack_from('<I',img,la)[0]; lits[la]=w
            if w&1 and lo<=w<hi and (w&~1) not in insn:
                pass
        a+=i.size
while work:
    walk(work.pop())
# code pointers in literals & in data tables (aligned words pointing to odd addr in range preceded by push)
changed=True
while changed:
    changed=False
    for la,w in list(lits.items()):
        if w&1 and lo<=w<hi and (w&~1) not in insn:
            funcs.add(w&~1); work.append(w&~1); changed=True
    while work: walk(work.pop())
with open(out,'w') as f:
    prev=None
    for a in sorted(insn):
        i=insn[a]
        if prev is not None and a!=prev:
            # dump gap as data words
            g=prev
            while g<a:
                if g%4==0 and g+4<=a:
                    w=struct.unpack_from('<I',img,g)[0]
                    f.write('%05x:   .word 0x%08x  %s\n'%(g,w,pname(w))); g+=4
                else:
                    n=min(2,a-g); f.write('%05x:   .byte %s\n'%(g,img[g:g+n].hex())); g+=n
        if a in funcs:
            f.write('\n; ===== FUNC %05x  callers: %s\n'%(a,' '.join('%x'%x for x in sorted(xref[a]))))
        elif a in xref:
            f.write('; <- %s %s\n'%(' '.join('%x'%x for x in sorted(xref[a])),' '.join(swt.get(a,[]))))
        c=''
        ops=i.operands
        if i.mnemonic=='ldr' and len(ops)==2 and ops[1].type==ARM_OP_MEM and ops[1].mem.base==ARM_REG_PC:
            la=((a+4)&~3)+ops[1].mem.disp; w=struct.unpack_from('<I',img,la)[0]
            c='  ; =0x%08x %s'%(w,pname(w))
        f.write('%05x:   %-8s %s%s\n'%(a,i.mnemonic,i.op_str,c))
        prev=a+i.size
print('insns',len(insn),'funcs',len(funcs))
