import pickle, re, collections
secs,fn2sec,insns=pickle.load(open('db.pkl','rb'))
MEMOPS=set("lb lbu lh lhu lw lwu ld sb sh sw sd lwc1 swc1 ldc1 sdc1 lwl lwr swl swr".split())
def regs(asm):
    p=asm.split(None,1); op=p[0]; args=[a.strip() for a in p[1].split(',')] if len(p)>1 else []
    return op,args
def imm(s): return int(s,0)
refs=[]   # (ea, sec, func, addr, op, asm, basereg_combined)
byfunc=collections.defaultdict(list)
for ins in insns: byfunc[(ins[0],ins[1])].append(ins)
for key,lst in byfunc.items():
    lui={}   # reg -> hi value
    comb={}  # reg -> hi value (after addu with index)
    kill_pending=False
    kill_after=0          # a call's clobbers take effect after its delay slot (which runs before the call)
    for sec,fn,a,asm in lst:
        if kill_after:
            kill_after-=1
        if kill_after==0 and kill_pending:
            kill_pending=False
            for r in list(lui):
                if r not in ('$s0','$s1','$s2','$s3','$s4','$s5','$s6','$s7','$fp','$gp'): lui.pop(r)
            for r in list(comb):
                if r not in ('$s0','$s1','$s2','$s3','$s4','$s5','$s6','$s7','$fp','$gp'): comb.pop(r)
        op,args=regs(asm)
        if op in ('jal','jalr'): kill_after=2; kill_pending=True
        if op=='lui':
            lui[args[0]]=imm(args[1])<<16; comb.pop(args[0],None); continue
        if op in MEMOPS and len(args)==2:
            m=re.match(r'(-?0x[0-9A-Fa-f]+|-?\d+)\((\$\w+)\)',args[1])
            if m:
                off=imm(m.group(1)); r=m.group(2)
                if r in lui: refs.append(((lui[r]+off)&0xFFFFFFFF,sec,fn,a,op,asm,'direct'))
                elif r in comb: refs.append(((comb[r]+off)&0xFFFFFFFF,sec,fn,a,op,asm,'indexed'))
            if op.startswith('l') and args[0] in lui: lui.pop(args[0]); 
            if op.startswith('l'): comb.pop(args[0],None)
            continue
        if op=='addiu' and len(args)==3 and args[1] in lui:
            ea=(lui[args[1]]+imm(args[2]))&0xFFFFFFFF
            refs.append((ea,sec,fn,a,op,asm,'addr'))
            # result is an address
            d=args[0]; lui.pop(d,None); comb[d]=ea - 0 if False else None
            comb.pop(d,None)
            continue
        if op=='addiu' and len(args)==3 and args[1] in comb and comb[args[1]] is not None:
            # hi + index, then the low half: an indexed address (lui; addu idx; addiu lo)
            ea=(comb[args[1]]+imm(args[2]))&0xFFFFFFFF
            refs.append((ea,sec,fn,a,op,asm,'indexed'))
            lui.pop(args[0],None); comb.pop(args[0],None); continue
        if op=='ori' and len(args)==3 and args[1] in lui:
            ea=(lui[args[1]]|imm(args[2]))&0xFFFFFFFF
            refs.append((ea,sec,fn,a,op,asm,'addr'))
            lui.pop(args[0],None); comb.pop(args[0],None); continue
        if op in ('addu','add') and len(args)==3 and (args[1] in lui or args[2] in lui):
            hi=lui.get(args[1], lui.get(args[2]))
            d=args[0]; lui.pop(d,None); comb[d]=hi; continue
        # any other write to a reg kills it
        if args and args[0].startswith('$') and op not in ('sw','sh','sb','beq','bne','jr','jal','j','blez','bgtz','bltz','bgez','beqz','bnez','mtc1','sd','swc1','sdc1','bc1t','bc1f','bnel','beql'):
            lui.pop(args[0],None); comb.pop(args[0],None)
pickle.dump(refs,open('refs.pkl','wb'))
print(len(refs))
