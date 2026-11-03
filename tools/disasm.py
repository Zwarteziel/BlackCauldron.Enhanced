# Validates AGI v2 logic parsing for a given opcode arg table.
import sys
import os
G=os.environ.get('BC_GAMEDIR','.')+'/'
def dirn(f):
    b=open(G+f,'rb').read(); out={}
    for i in range(0,len(b),3):
        e=b[i:i+3]
        if e==b'\xff\xff\xff': continue
        out[i//3]=(e[0]>>4,((e[0]&15)<<16)|(e[1]<<8)|e[2])
    return out
vols=[open(G+f'VOL.{i}','rb').read() for i in range(3)]
def res(d,n):
    v,o=d[n]; b=vols[v]; L=b[o+3]|(b[o+4]<<8); return b[o+5:o+5+L]
CMD=[0,1,1,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,1,1,1,1,0,1,1,3,3,3,3,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,3,1,1,1,2,1,2,2,1,1,2,2,5,5,3,1,1,2,2,1,1,4,0,1,1,1,2,2,2,1,2,0,1,1,3,3,3,0,0,1,2,1,3,0,0,2,5,2,1,2,0,0,3,7,7,0,0,0,0,0,1,1,3,0,0,1,1,0,0,0,0,0,0,0,1,1,1,0,0,3,3,0,3,4,4,1,5,2,1,2,0,1,1,0,1,0,0,2,2,2,2,0,1,0,0,0,0,0,0,1,0,4,2,0]
TST=[0,2,2,2,2,2,2,1,1,1,2,5,1,0,-1,2,5,5,5]
def check(pa, quit_args):
    cmd=CMD[:]; cmd[151]=pa; cmd[152]=pa; cmd[134]=quit_args
    logs=dirn('LOGDIR'); bad=0; maxop=0
    for n in logs:
        d=res(logs,n); L=d[0]|d[1]<<8; code=d[2:2+L]; mo=2+L; nmsg=d[mo]
        i=0; ok=True
        while i<len(code):
            c=code[i]; i+=1
            if c==0xFF:
                while True:
                    t=code[i]; i+=1
                    if t==0xFF: break
                    if t in (0xFC,0xFD): continue
                    if t>18: ok=False; break
                    if t==14: k=code[i]; i+=1+2*k
                    else: i+=TST[t]
                if not ok: break
                i+=2
            elif c==0xFE: i+=2
            elif c<len(cmd):
                maxop=max(maxop,c)
                if c in (101,) and not (1<=code[i]<=nmsg): ok=False;break
                if c==103 and not (1<=code[i+2]<=nmsg): ok=False;break
                if c==151 and not (1<=code[i]<=nmsg): ok=False;break
                i+=cmd[c]
            else: ok=False; break
        if not ok or i!=len(code): bad+=1
    return bad,maxop
if 0:
  for pa in (3,4):
    for q in (0,1): print('print.at',pa,'quit',q,'-> bad logics, maxop',check(pa,q))
