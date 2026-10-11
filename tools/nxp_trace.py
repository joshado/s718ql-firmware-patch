#!/usr/bin/env python3
"""Condense nxp_disasm.py output into a per-function call trace.

Tracks constant r0-r3 across straight-line code so GPIO writes/reads, delays
and RAM flag accesses print with their arguments, e.g. gpio_wr(P1_5, 0).
Pin ids are (port << 8) | pin. Extra names: ADDR=name on the command line.

usage: nxp_trace.py app.txt [ADDR=name ...] > trace.txt
"""
import re,sys
names={0x48ba:'gpio_wr',0x4780:'gpio_rd',0x8130:'delay_ticks',0x3cf0:'led_mode',0x77e0:'sel77e0',0x776e:'sel776e',0x7278:'POWER_ON',0x7370:'POWER_OFF',0x724c:'clr_on_flags',0x40e8:'iocon_cfg',0x46b4:'gpio_dir?',0x87ec:'switch'}
for a in sys.argv[2:]:
    k,v=a.split('='); names[int(k,16)]=v
def pin(x): return 'P%d_%d'%(x>>8,x&0xff)
reg={}; 
want=None
for line in open(sys.argv[1]):
    m=re.match(r'; ===== FUNC (\w+)\s+callers: (.*)',line)
    if m:
        print('\n== %s  (callers %s)'%(m.group(1),m.group(2).strip()[:160])); reg={}; continue
    if line.startswith(';'):
        reg={}; print('  @'+line[4:].strip()[:70]); continue
    m=re.match(r'(\w+):\s+(\S+)\s*(.*?)(?:\s+; =0x(\w+).*)?$',line.rstrip())
    if not m: continue
    addr,mn,ops,lit=m.groups(); o=[x.strip() for x in ops.split(',')] if ops else []
    def imm(s): return int(s.lstrip('#'),0)
    try:
        if mn=='movs' and o[1].startswith('#'): reg[o[0]]=imm(o[1])
        elif mn=='ldr' and lit: reg[o[0]]=int(lit,16)
        elif mn in('adds','subs') and len(o)==2 and o[1].startswith('#') and o[0] in reg:
            reg[o[0]]=reg[o[0]]+imm(o[1]) if mn=='adds' else reg[o[0]]-imm(o[1])
        elif mn in('adds','subs') and len(o)==3 and o[2].startswith('#') and o[1] in reg:
            reg[o[0]]=reg[o[1]]+imm(o[2]) if mn=='adds' else reg[o[1]]-imm(o[2])
        elif mn=='lsls' and len(o)==3 and o[2].startswith('#') and o[1] in reg: reg[o[0]]=(reg[o[1]]<<imm(o[2]))&0xffffffff
        elif mn in('mov','movs') and len(o)==2 and o[1] in reg: reg[o[0]]=reg[o[1]]
        elif mn=='bl':
            t=imm(o[0]); n=names.get(t,'f_%04x'%t)
            a=[reg.get(r) for r in('r0','r1','r2','r3')]
            if n=='gpio_wr': s='gpio_wr(%s, %s)'%(pin(a[0]) if a[0] is not None else '?',a[1])
            elif n=='gpio_rd': s='gpio_rd(%s)'%(pin(a[0]) if a[0] is not None else '?')
            elif n=='delay_ticks': s='delay_ticks(%s)'%a[0]
            else: s='%s(%s)'%(n,', '.join('?' if x is None else hex(x) for x in a[:3]))
            print('  %s: %s'%(addr,s)); reg={}
        elif mn in('cmp',) : print('  %s:   cmp %s'%(addr,ops))
        elif mn in('strb','strh','str') and o[0] in reg and len(o)>=2:
            base=o[1].strip('[]'); 
            if base in reg and reg[base]>=0x10000000: print('  %s:   [%08x%s] = %s'%(addr,reg[base],('+'+o[2].strip(']')) if len(o)>2 else '',hex(reg[o[0]])))
        elif mn in('ldrb','ldrh','ldr') and len(o)>=2 and o[1].strip('[]') in reg and reg[o[1].strip('[]')]>=0x10000000 and not lit:
            print('  %s:   rd [%08x%s]'%(addr,reg[o[1].strip('[]')],('+'+o[2].strip(']')) if len(o)>2 else '')); reg.pop(o[0],None)
        elif o and o[0] in reg and mn not in('cmp','tst','str','strb','strh','push','b','bne','beq'): reg.pop(o[0],None)
    except Exception as e: pass
