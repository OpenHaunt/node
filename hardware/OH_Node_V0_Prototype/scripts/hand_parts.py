#!/usr/bin/env python3
"""Hand-sourced parts list (everything JLC does not assemble) with live DigiKey prices.

Usage:  python3 scripts/hand_parts.py [--boards N] [--netlist FILE]
Reads PARTDB_TOKEN, DIGIKEY_CLIENT_ID/SECRET from ../../.env (repo root).
Writes production/jlc_final/hand_parts.csv and hand_parts.md.
"""
import os,sys,re,csv,json,argparse,subprocess,urllib.request,urllib.parse,collections
from datetime import date
HERE=os.path.dirname(os.path.abspath(__file__)); PROJ=os.path.dirname(HERE)
ROOT=os.path.dirname(os.path.dirname(PROJ))
for line in open(os.path.join(ROOT,".env")):
    if "=" in line and not line.startswith("#"): k,v=line.rstrip("\n").split("=",1); os.environ.setdefault(k,v)
B="https://parts.hive.service42.me"
ap=argparse.ArgumentParser(); ap.add_argument("--boards",type=int,default=1); ap.add_argument("--netlist"); a=ap.parse_args()
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
attr={m.group(2):m.group(3).strip() for m in re.finditer(r'\n\t\(footprint "([^"]+)"[\s\S]*?\(property "Reference" "([^"]+)"[\s\S]*?\n\t\t\(attr([^\n]*)\)',pcb)}
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
    p=pget(f"{B}/api/parts/{pid}"); stock=int(sum(l.get("amount") or 0 for l in p.get("partLots",[])))
    dk=[o for o in p["orderdetails"] if o["supplier"]["name"]=="DigiKey"]; dkpn=dk[0]["supplierpartnr"] if dk else ""
    qty=len(refs)*a.boards; need=max(0,qty-stock)
    price1=priceN=avail=""; tiers=[]
    try:
        d=json.loads(urllib.request.urlopen(urllib.request.Request(f"https://api.digikey.com/products/v4/search/{urllib.parse.quote(p['name'])}/productdetails",headers=DH),timeout=60).read())["Product"]
        vs=d.get("ProductVariations",[]); var=min(vs,key=lambda x:x.get("MinimumOrderQuantity") or 1) if vs else {}
        dkpn=dkpn or var.get("DigiKeyProductNumber",""); avail=d.get("QuantityAvailable")
        tiers=sorted((s["BreakQuantity"],s["UnitPrice"]) for s in var.get("StandardPricing",[]))
        if tiers:
            price1=tiers[0][1]; priceN=next((pr for bq,pr in reversed(tiers) if bq<=max(need,1)),tiers[0][1])
    except Exception as e: avail=f"lookup failed: {e}"
    rows.append({"refs":", ".join(sorted(refs,key=refkey)),"qty_per_board":len(refs),"qty_total":qty,"part":p["name"],"description":(p.get("description") or "")[:60],"partdb_id":pid,"partdb_stock":stock,"to_buy":need,"digikey_pn":dkpn,"digikey_stock":avail,"price_1":price1,"price_at_qty":priceN,"line_total":round(need*priceN,2) if need and priceN!="" else 0,"tiers":"; ".join(f"{bq}:{pr}" for bq,pr in tiers)})
out=os.path.join(PROJ,"production","jlc_final"); os.makedirs(out,exist_ok=True)
with open(os.path.join(out,"hand_parts.csv"),"w",newline="") as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
total=sum(r["line_total"] for r in rows)
with open(os.path.join(out,"hand_parts.md"),"w") as f:
    f.write(f"# Hand-sourced parts — {a.boards} board(s), DigiKey prices (EUR) fetched {date.today()}\n\nGenerated by `scripts/hand_parts.py --boards {a.boards}`. Stock from Part-DB; `to buy` = needed minus stock.\n\n")
    f.write("| Refs | Per board | Total | Part | Part-DB | Stock | To buy | DigiKey | Stock DK | € @1 | € at qty | Line € |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n")
    for r in rows: f.write(f"| {r['refs']} | {r['qty_per_board']} | {r['qty_total']} | {r['part']} | {r['partdb_id']} | {r['partdb_stock']} | {r['to_buy']} | {r['digikey_pn']} | {r['digikey_stock']} | {r['price_1']} | {r['price_at_qty']} | {r['line_total']:.2f} |\n")
    f.write(f"\n**DigiKey order total for {a.boards} board(s): €{total:.2f}** (excl. shipping/VAT). Plus 4 × M2.5 screws per board (H1–H4).\n\nDNP, not fitted: {', '.join(sorted(dnp,key=refkey))}.\n")
print(f"{len(rows)} lines, to buy for {a.boards} board(s): €{total:.2f} -> {out}/hand_parts.csv + .md")
for r in rows: print(f"  {r['refs']:28s} {r['part']:22s} need {r['qty_total']:2d} stock {r['partdb_stock']:3d} buy {r['to_buy']:2d}  @{r['price_at_qty']} = {r['line_total']}")
