import os,json,re,csv,sys,urllib.request,collections
sys.path.insert(0,"/Users/hannes/.claude/plugins/cache/kicad-happy/kicad-happy/2.2.1/skills/kicad/scripts")
from sexp_parser import parse_file
B="https://parts.hive.service42.me"; T=os.environ["PARTDB_TOKEN"]
def get(u): return json.loads(urllib.request.urlopen(urllib.request.Request(u,headers={"Authorization":f"Bearer {T}","Accept":"application/json"}),timeout=60).read())
def fa(n,k): return [c for c in n if isinstance(c,list) and c and c[0]==k]
def f1(n,k): r=fa(n,k); return r[0] if r else None
def v(n,k): r=f1(n,k); return r[1] if r and len(r)>1 else None
NET,PCB,SCH,OUT=sys.argv[1:5]; HAND={"PS1"}
t=parse_file(NET); sch=open(SCH).read(); dnp=set()
for m in re.finditer(r'\(symbol\s*\n',sch):
    a=m.start(); d=0
    for b in range(a,len(sch)):
        if sch[b]=='(': d+=1
        elif sch[b]==')':
            d-=1
            if d==0: b+=1; break
    blk=sch[a:b]
    if '(lib_id' in blk and re.search(r'\n\t\t\(dnp yes\)',blk): dnp.add(re.search(r'\(property "Reference" "([^"]+)"',blk).group(1))
# Footprint attributes, parsed per footprint block by paren matching. A regex scanning forward from a
# footprint without an (attr ...) line (test points) used to take the next footprint's attr and drop
# that footprint from the BOM.
pcb=open(PCB).read(); attr={}
for m in re.finditer(r'\n\t\(footprint "',pcb):
    a=m.start()+1; d=0
    for b in range(a,len(pcb)):
        if pcb[b]=='(': d+=1
        elif pcb[b]==')':
            d-=1
            if d==0: b+=1; break
    blk=pcb[a:b]; r=re.search(r'\(property "Reference" "([^"]+)"',blk); at=re.search(r'\n\t\t\(attr([^\n]*)\)',blk)
    if r: attr[r.group(1)]=at.group(1).strip() if at else ""
rows=[]; cache={}
for c in fa(f1(t,"components"),"comp"):
    ref=v(c,"ref"); props={x[1][1]:x[2][1] for x in fa(c,"property") if len(x)>2}; pid=props.get("Part-DB ID")
    kind="DNP" if ref in dnp else "hand" if ref in HAND else "THT" if "through_hole" in attr.get(ref,"") else "SMD" if "smd" in attr.get(ref,"") else "other"
    if pid and pid not in cache:
        p=get(f"{B}/api/parts/{pid}"); l=[o["supplierpartnr"] for o in p["orderdetails"] if o["supplier"]["name"]=="LCSC"]; cache[pid]=(p["name"],l[0] if l else "")
    rows.append((ref,v(c,"value"),v(c,"footprint").split(":")[-1],kind,cache.get(pid,("",""))))
bom=collections.OrderedDict()
for ref,val,fp,kind,(mpn,lcsc) in rows:
    if kind=="SMD": bom.setdefault((val,fp,lcsc,mpn),[]).append(ref)
def key(r): return (r.rstrip("0123456789"),int("".join(ch for ch in r if ch.isdigit()) or 0))
with open(OUT,"w",newline="") as f:
    w=csv.writer(f); w.writerow(["Comment","Designator","Footprint","LCSC Part #","MPN"])
    for (val,fp,lcsc,mpn),refs in bom.items(): w.writerow([val,",".join(sorted(refs,key=key)),fp,lcsc,mpn])
print("BOM lines",len(bom),"| SMD parts",sum(len(r) for r in bom.values()),"| missing LCSC:",[(k[3],refs) for k,refs in bom.items() if not k[2]])
print("excluded:",{k:sorted([r[0] for r in rows if r[3]==k],key=key) for k in ("DNP","hand","THT","other")})
