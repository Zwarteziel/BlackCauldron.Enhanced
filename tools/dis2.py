import sys
from ops import CMD,TST
from disasm import dirn,res
def parse(n, over={}, verbose=False):
    d=res(dirn('LOGDIR'),n); L=d[0]|d[1]<<8; code=d[2:2+L]
    i=0; out=[]
    while i<len(code):
        st=i; c=code[i]; i+=1
        if c==0xFF:
            s='if '
            while True:
                t=code[i]; i+=1
                if t==0xFF: break
                if t==0xFC: s+='|| '; continue
                if t==0xFD: s+='! '; continue
                if t>=len(TST): return ('BADTEST',st,t,out[-5:])
                nm,a=TST[t]
                if t==14: k=code[i]; s+=f'said{list(code[i+1:i+1+2*k])} '; i+=1+2*k
                else: s+=f'{nm}{list(code[i:i+len(a)])} '; i+=len(a)
            off=code[i]|code[i+1]<<8; i+=2; out.append((st,s+f' skip {off}'))
        elif c==0xFE:
            off=code[i]|code[i+1]<<8; i+=2; out.append((st,f'else/goto {off}'))
        elif c<len(CMD):
            nm,a=CMD[c]; na=over.get(nm,len(a)); out.append((st,f'{nm}{list(code[i:i+na])}')); i+=na
        else: return ('BADCMD',st,c,out[-5:])
    return ('OK' if i==len(code) else 'LEN',i,len(code),out[-3:])
if __name__=='__main__':
    over={}
    for a in sys.argv[1:]:
        k,v=a.split('='); over[k]=int(v)
    bad=[]
    for n in sorted(dirn('LOGDIR')):
        r=parse(n,over)
        if r[0]!='OK': bad.append((n,r))
    print(len(bad),'bad')
    for b in bad[:3]: print(b)
