#!/usr/bin/env python3
"""Decompress the NXP app .data initialisers (zero-run RLE, handler 0x23d6).

Region table is at 0x997c: src 0x999c -> RAM 0x10000100, 0x1fc bytes. Prints
the non-zero rows, i.e. the power-on default of every initialised variable.

usage: nxp_initdata.py nxp_lpc11u67.bin OUT.bin
"""
import sys
d=open(sys.argv[1],'rb').read()
src=0x999c; dst=0x10000100; ln=0x1fc
out=bytearray(); p=src
while len(out)<ln:
    b=d[p]; p+=1
    lit=b&0xf
    if lit==0: lit=d[p]; p+=1
    z=b>>4
    if z==0: z=d[p]; p+=1
    out+=d[p:p+lit-1]; p+=lit-1
    out+=bytes(z-1)
open(sys.argv[2],'wb').write(out)
print('consumed to',hex(p),'len',hex(len(out)))
for i in range(0,len(out),16):
    row=out[i:i+16]
    if any(row): print('%08x  %s'%(dst+i,row.hex(' ')))
