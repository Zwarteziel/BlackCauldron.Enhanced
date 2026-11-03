from dis2 import res, dirn
from ops import CMD, TST
def msgs(n):
    d=res(dirn('LOGDIR'),n); L=d[0]|d[1]<<8; mo=2+L; nm=d[mo]; base=mo+1
    key=b'Avis Durgan'; text=base+2+2*nm; dd=bytearray(d)
    for i in range(text,len(dd)): dd[i]^=key[(i-text)%11]
    m={}
    for i in range(nm):
        o=dd[base+2+2*i]|dd[base+3+2*i]<<8
        if o: s=dd[base+o:]; m[i+1]=bytes(s[:s.index(0)]).decode('latin1')
    return m
def listing(n):
    d=res(dirn('LOGDIR'),n); L=d[0]|d[1]<<8; code=d[2:2+L]; i=0; out=[]; M=msgs(n)
    while i<len(code):
        st=i; c=code[i]; i+=1
        if c==0xFF:
            s=[]
            while code[i]!=0xFF:
                t=code[i]; i+=1
                if t==0xFC: s.append('||'); continue
                if t==0xFD: s.append('!'); continue
                if t==14: k=code[i]; s.append('said'); i+=1+2*k; continue
                nm,a=TST[t]; s.append(f'{nm}({",".join(map(str,code[i:i+len(a)]))})'); i+=len(a)
            i+=1; off=code[i]|code[i+1]<<8; i+=2
            out.append((st,f'if {" ".join(s)} [skip {off} -> {i+off}]'))
        elif c==0xFE:
            off=code[i]|code[i+1]<<8; off=off-65536 if off>32767 else off; i+=2; out.append((st,f'goto {i+off}'))
        else:
            nm,a=CMD[c]; args=list(code[i:i+len(a)]); i+=len(a)
            ex=''
            if nm in('print','set.menu','set.menu.item','display','print.at','get.num','set.string','get.string') :
                mi=args[2] if nm=='display' else (args[1] if nm in('set.string','get.string') else args[0]); ex=' ; '+repr(M.get(mi,'')[:70])
            out.append((st,f'{nm}({",".join(map(str,args))}){ex}'))
    return out
if __name__=='__main__':
    import sys
    for st,l in listing(int(sys.argv[1])): print(st,l)
