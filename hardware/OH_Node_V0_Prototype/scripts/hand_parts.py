#!/usr/bin/env python3
"""Hand-sourced parts list (everything JLC does not assemble) with live DigiKey prices.

Usage:  python3 scripts/hand_parts.py [--boards N] [--spares] [--netlist FILE]
Reads PARTDB_TOKEN, DIGIKEY_CLIENT_ID/SECRET from ../../.env (repo root).
Writes production/jlc_final/hand_parts.csv, hand_parts.md, hand_parts_digikey.csv (DigiKey cart upload) and
hand_parts_wurth.csv (direct order). Würth Elektronik parts are bought from Würth directly, everything else
from DigiKey; the manufacturer comes from Part-DB. DigiKey prices for Würth parts are a reference only.
--spares adds spares per line by unit price: 1 from €5, 2 from €1, 3 below.
"""
import os,sys,re,csv,json,argparse,subprocess,urllib.request,urllib.parse,collections
from datetime import date
HERE=os.path.dirname(os.path.abspath(__file__)); PROJ=os.path.dirname(HERE)
ROOT=os.path.dirname(os.path.dirname(PROJ))
for line in open(os.path.join(ROOT,".env")):
    if "=" in line and not line.startswith("#"): k,v=line.rstrip("\n").split("=",1); os.environ.setdefault(k,v)
B="https://parts.hive.service42.me"
ap=argparse.ArgumentParser(); ap.add_argument("--boards",type=int,default=1); ap.add_argument("--netlist"); ap.add_argument("--spares",action="store_true"); a=ap.parse_args()
SPARES=((5.0,1),(1.0,2),(0.0,3))   # unit price threshold in EUR -> spares per line
HAND={"PS1"}   # SMD parts deliberately hand-soldered
# --- netlist (export fresh unless given)
net=a.netlist
if not net:
    net=os.path.join(PROJ,"analysis","kc","net_hand.net"); os.makedirs(os.path.dirname(net),exist_ok=True)
    env=dict(os.environ,LRV_CUSTOM_LIBS=os.environ.get("LRV_CUSTOM_LIBS","/Users/hannes/source/ffp/kicad-custom-libs"))
    subprocess.run(["/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli","sch","export","netlist","--format","kicadsexpr","-o",net,os.path.join(PROJ,"OH_Node_V0_Prototype.kicad_sch")],env=env,check=True,capture_output=True)
sys.path.insert(0,"/Users/hannes/.claude/plugins/cache/kicad-happy/kicad-happy/2.2.1/skills/kicad/scripts")
from sexp_parser import parse_file
def fa(n,k): return [c for c in n if isinstance(c,list) and c and c[0]==k]
def f1(n,k): r=fa(n,k); return r[0] if r else None
def v(n,k): r=f1(n,k); return r[1] if r and len(r)>1 else None
t=parse_file(net)
pcb=open(os.path.join(PROJ,"OH_Node_V0_Prototype.kicad_pcb")).read()
# Footprint attributes per footprint block (paren matching). A forward regex took the next footprint's
# attr for footprints without an (attr ...) line, such as the test points.
attr={}
for m in re.finditer(r'\n\t\(footprint "',pcb):
    s0=m.start()+1; d=0
    for b in range(s0,len(pcb)):
        if pcb[b]=='(': d+=1
        elif pcb[b]==')':
            d-=1
            if d==0: b+=1; break
    blk=pcb[s0:b]; rr=re.search(r'\(property "Reference" "([^"]+)"',blk); at=re.search(r'\n\t\t\(attr([^\n]*)\)',blk)
    if rr: attr[rr.group(1)]=at.group(1).strip() if at else ""
sch=open(os.path.join(PROJ,"OH_Node_V0_Prototype.kicad_sch")).read(); dnp=set()
for m in re.finditer(r'\(symbol\s*\n',sch):
    s0=m.start(); d=0
    for b in range(s0,len(sch)):
        if sch[b]=='(': d+=1
        elif sch[b]==')':
            d-=1
            if d==0: b+=1; break
    blk=sch[s0:b]
    if '(lib_id' in blk and re.search(r'\n\t\t\(dnp yes\)',blk): dnp.add(re.search(r'\(property "Reference" "([^"]+)"',blk).group(1))
groups=collections.OrderedDict()
for c in fa(f1(t,"components"),"comp"):
    r=v(c,"ref"); props={x[1][1]:x[2][1] for x in fa(c,"property") if len(x)>2}
    if r in dnp or r.startswith("H"): continue
    if r in HAND or "through_hole" in attr.get(r,""): groups.setdefault(props.get("Part-DB ID"),[]).append(r)
def pget(u): return json.loads(urllib.request.urlopen(urllib.request.Request(u,headers={"Authorization":"Bearer "+os.environ["PARTDB_TOKEN"],"Accept":"application/json"}),timeout=60).read())
tok=json.loads(urllib.request.urlopen(urllib.request.Request("https://api.digikey.com/v1/oauth2/token",data=urllib.parse.urlencode({"client_id":os.environ["DIGIKEY_CLIENT_ID"],"client_secret":os.environ["DIGIKEY_CLIENT_SECRET"],"grant_type":"client_credentials"}).encode(),headers={"Content-Type":"application/x-www-form-urlencoded"})).read())["access_token"]
DH={"Authorization":"Bearer "+tok,"X-DIGIKEY-Client-Id":os.environ["DIGIKEY_CLIENT_ID"],"X-DIGIKEY-Locale-Site":"DE","X-DIGIKEY-Locale-Currency":"EUR"}
def refkey(r): return (r.rstrip("0123456789"),int("".join(ch for ch in r if ch.isdigit()) or 0))
rows=[]
for pid,refs in groups.items():
    p=pget(f"{B}/api/parts/{pid}")
    dk=[o for o in p["orderdetails"] if o["supplier"]["name"]=="DigiKey"]; dkpn=dk[0]["supplierpartnr"] if dk else ""
    qty=len(refs)*a.boards
    price1=priceN=avail=""; tiers=[]
    try:
        d=json.loads(urllib.request.urlopen(urllib.request.Request(f"https://api.digikey.com/products/v4/search/{urllib.parse.quote(p['name'])}/productdetails",headers=DH),timeout=60).read())["Product"]
        vs=d.get("ProductVariations",[]); var=min(vs,key=lambda x:x.get("MinimumOrderQuantity") or 1) if vs else {}
        dkpn=dkpn or var.get("DigiKeyProductNumber",""); avail=d.get("QuantityAvailable")
        tiers=sorted((s["BreakQuantity"],s["UnitPrice"]) for s in var.get("StandardPricing",[]))
        if tiers: price1=tiers[0][1]
    except Exception as e: avail=f"lookup failed: {e}"
    def tier(n): return next((pr for bq,pr in reversed(tiers) if bq<=max(n,1)),tiers[0][1]) if tiers else ""
    spares=0
    if a.spares and tiers: spares=next(n for th,n in SPARES if tier(qty)>=th)
    order=qty+spares; priceN=tier(order)
    unit=priceN if priceN!="" else None
    mfr=(p.get("manufacturer") or {}).get("name","") if isinstance(p.get("manufacturer"),dict) else ""
    src="Würth direct" if re.search(r"w[uü]rth",mfr,re.I) else "DigiKey"
    rows.append({"refs":", ".join(sorted(refs,key=refkey)),"qty_per_board":len(refs),"needed":qty,"spares":spares,"order":order,"part":p["name"],"manufacturer":mfr,"source":src,"description":(p.get("description") or "")[:60],"partdb_id":pid,"digikey_pn":dkpn,"digikey_stock":avail,"price_1":price1,"unit_price":priceN,"cost_needed":round(order*unit,2) if unit is not None else "","tiers":"; ".join(f"{bq}:{pr}" for bq,pr in tiers)})
out=os.path.join(PROJ,"production","jlc_final"); os.makedirs(out,exist_ok=True)
with open(os.path.join(out,"hand_parts.csv"),"w",newline="") as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
dkrows=[r for r in rows if r["source"]=="DigiKey"]; werows=[r for r in rows if r["source"]!="DigiKey"]
# DigiKey cart upload (digikey.de > Cart > Upload a list): full quantity for the boards, no stock deduction
with open(os.path.join(out,"hand_parts_digikey.csv"),"w",newline="") as f:
    w=csv.writer(f); w.writerow(["Digi-Key Part Number","Manufacturer Part Number","Quantity","Customer Reference"])
    for r in dkrows: w.writerow([r["digikey_pn"],r["part"],r["order"],r["refs"][:48]])
# Würth direct order: order code is the MPN
with open(os.path.join(out,"hand_parts_wurth.csv"),"w",newline="") as f:
    w=csv.writer(f); w.writerow(["Order Code","Description","Quantity","Customer Reference"])
    for r in werows: w.writerow([r["part"],r["description"],r["order"],r["refs"]])
nodk=[r["part"] for r in dkrows if not r["digikey_pn"]]
tot_dk=sum(r["cost_needed"] or 0 for r in dkrows); ref_we=sum(r["cost_needed"] or 0 for r in werows)
def table(f,rs):
    f.write("| Refs | Per board | Needed | Spares | Order | Part | Manufacturer | Part-DB | DigiKey | DK stock | € unit | € order |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n")
    for r in rs: f.write(f"| {r['refs']} | {r['qty_per_board']} | {r['needed']} | {r['spares']} | {r['order']} | {r['part']} | {r['manufacturer']} | {r['partdb_id']} | {r['digikey_pn']} | {r['digikey_stock']} | {r['unit_price']} | {r['cost_needed']} |\n")
with open(os.path.join(out,"hand_parts.md"),"w") as f:
    f.write(f"# Hand-soldered THT parts — {a.boards} board(s), DigiKey prices (EUR) fetched {date.today()}\n\nGenerated by `scripts/hand_parts.py --boards {a.boards}{' --spares' if a.spares else ''}`. Order = needed + spares"
            f"{' (1 from €5 unit price, 2 from €1, 3 below)' if a.spares else ''}; cost = order × unit price at that quantity.\n\n")
    f.write(f"## From DigiKey\n\nCart upload: `hand_parts_digikey.csv`.\n\n"); table(f,dkrows)
    f.write(f"\n**DigiKey cost for {a.boards} board(s): €{tot_dk:.2f}**, excl. shipping/VAT.\n\n")
    f.write(f"## From Würth Elektronik directly\n\nOrder list: `hand_parts_wurth.csv` (order code = MPN). The DigiKey prices are a reference only.\n\n"); table(f,werows)
    f.write(f"\nReference at DigiKey prices: €{ref_we:.2f}.\n\nPlus 4 × M2.5 screws per board (H1–H4).\n\nDNP, not fitted: {', '.join(sorted(dnp,key=refkey))}.\n")
print(f"{len(rows)} lines for {a.boards} board(s): DigiKey €{tot_dk:.2f} ({len(dkrows)} lines), Würth direct {len(werows)} lines (DK reference €{ref_we:.2f}) -> {out}/hand_parts.csv + .md")
print(f"DigiKey cart: {len(dkrows)} lines -> {out}/hand_parts_digikey.csv" + (f"  (no DigiKey PN, map by MPN: {', '.join(nodk)})" if nodk else ""))
print(f"Würth order: {len(werows)} lines -> {out}/hand_parts_wurth.csv")
for r in rows: print(f"  {r['source']:13s} {r['refs']:28s} {r['part']:22s} needed {r['needed']:2d} +{r['spares']} = {r['order']:2d}  @{r['unit_price']} = {r['cost_needed']}")
