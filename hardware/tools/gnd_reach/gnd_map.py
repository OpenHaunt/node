# GND pour analysis for the outer copper layers.
#
# - Reach: every GND pixel (zone fill, GND tracks, GND pads) is coloured by the distance *through copper* to the
#   nearest GND via or plated GND hole. Inner layers are assumed to be solid GND planes, so a via is a plane
#   connection. Long reach = a pour finger that only connects at one end ("antenna").
# - Thin: parts of the GND zone fill where a 0.6 mm disc does not fit (necks, slivers, fingers).
# - Pads: for every SMD GND pad, edge-to-edge distance to the nearest GND via; > 1 mm yellow, > 2 mm red.
#
# Input is the JSON from export_copper.py. 20 px/mm; distance uses an octagonal metric (within ~4 % of Euclid).
import json, sys, math, numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as ndi
D=json.load(open(sys.argv[1])); outdir=sys.argv[2]
R=20.0  # px per mm
x0,y0,x1,y1=D['bbox']; W=int((x1-x0)*R)+1; H=int((y1-y0)*R)+1
P=lambda x,y:((x-x0)*R,(y-y0)*R)
def raster(polys):
    im=Image.new('1',(W,H),0); dr=ImageDraw.Draw(im)
    for p in polys:
        if p['h']:
            t=Image.new('1',(W,H),0); td=ImageDraw.Draw(t); td.polygon([P(*q) for q in p['o']],fill=1)
            for h in p['h']: td.polygon([P(*q) for q in h],fill=0)
            im=Image.fromarray(np.array(im)|np.array(t)); dr=ImageDraw.Draw(im)
        else: dr.polygon([P(*q) for q in p['o']],fill=1)
    return np.array(im,bool)
vias=[v for v in D['vias'] if v['gnd']]
try: font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',22); fsm=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
except Exception: font=fsm=ImageFont.load_default()
report={}
for L,lay in D['layers'].items():
    zone=raster(lay['gnd_zone']); gother=raster(lay['gnd_other']); other=raster(lay['other'])
    gnd=zone|gother
    src=np.zeros_like(gnd)
    yy,xx=np.mgrid[0:H,0:W]
    for v in vias:
        cx,cy=P(v['x'],v['y']); r=v['d']/2*R
        a,b_=int(max(cx-r-2,0)),int(min(cx+r+2,W)); c,d=int(max(cy-r-2,0)),int(min(cy+r+2,H))
        src[c:d,a:b_]|=((xx[c:d,a:b_]-cx)**2+(yy[c:d,a:b_]-cy)**2)<=r*r
    for p in lay['pads']:
        if p['tht']: src|=raster(p['poly'])
    src&=gnd
    # geodesic distance inside GND copper (octagonal metric, alternating cross/square)
    dist=np.full(gnd.shape,np.inf,np.float32); dist[src]=0; reached=src.copy()
    cross=ndi.generate_binary_structure(2,1); sq=ndi.generate_binary_structure(2,2)
    step=0
    while True:
        step+=1
        nxt=ndi.binary_dilation(reached,structure=(sq if step%2 else cross))&gnd&~reached
        if not nxt.any() or step>R*60: break
        dist[nxt]=step/R*0.93; reached|=nxt   # 0.93: octagonal metric correction
    # thin pour: zone pixels where a disk of diameter 0.6 mm does not fit
    rr=int(0.3*R); disk=(np.add.outer(np.arange(-rr,rr+1)**2,np.arange(-rr,rr+1)**2)<=rr*rr)
    thin=zone&~ndi.binary_opening(zone,structure=disk)
    # colour image
    img=np.zeros((H,W,3),np.uint8); img[:]=(18,18,18)
    img[other]=(70,70,70)
    def ramp(d):
        stops=[(0,(40,170,60)),(3,(230,220,40)),(6,(240,140,30)),(10,(220,40,40)),(20,(120,0,60))]
        out=np.zeros(d.shape+(3,),np.float32)
        for (a,ca),(b,cb) in zip(stops,stops[1:]):
            m=(d>=a)&(d<b); t=((d[m]-a)/(b-a))[:,None]; out[m]=np.array(ca)*(1-t)+np.array(cb)*t
        out[d>=stops[-1][0]]=stops[-1][1]; return out.astype(np.uint8)
    g=gnd&np.isfinite(dist); img[g]=ramp(dist[g])
    img[gnd&~np.isfinite(dist)]=(60,90,255)          # GND copper with no path to any via
    edge=thin&~ndi.binary_erosion(thin)               # outline thin sections
    img[thin]=(img[thin]*0.35+np.array([255,0,255])*0.65).astype(np.uint8)
    im=Image.fromarray(img); dr=ImageDraw.Draw(im)
    for v in vias:
        cx,cy=P(v['x'],v['y']); r=v['d']/2*R; dr.ellipse([cx-r,cy-r,cx+r,cy+r],outline=(255,255,255),width=2)
    # pad -> nearest via (edge to edge, straight) and geodesic distance at pad
    rows=[]
    for p in lay['pads']:
        if p['tht']: continue
        pts=np.array([q for pp in p['poly'] for q in pp['o']])
        best=min((np.min(np.hypot(pts[:,0]-v['x'],pts[:,1]-v['y']))-v['d']/2 for v in vias),default=99)
        m=raster(p['poly']); gd=float(np.min(dist[m])) if m.any() else math.inf
        rows.append((round(max(best,0),2),round(gd,2),p['ref'],p['num'],round(p['x'],2),round(p['y'],2)))
        if best>1.0:
            cx,cy=P(p['x'],p['y']); col=(255,60,60) if best>2.0 else (255,220,0)
            dr.ellipse([cx-28,cy-28,cx+28,cy+28],outline=col,width=4)
            dr.text((cx+30,cy-30),f"{p['ref']}.{p['num']} {best:.1f}",fill=col,font=fsm)
    rows.sort(reverse=True); report[L]=rows
    # legend + grid labels every 10 mm
    for gx in range(int(math.ceil(x0/10))*10,int(x1)+1,10): dr.text((P(gx,y0)[0]+2,2),str(gx),fill=(160,160,160),font=fsm)
    for gy in range(int(math.ceil(y0/10))*10,int(y1)+1,10): dr.text((2,P(x0,gy)[1]+2),str(gy),fill=(160,160,160),font=fsm)
    lx,ly=20,H-190
    dr.rectangle([lx-10,ly-10,lx+620,H-10],fill=(0,0,0))
    dr.text((lx,ly),f"{L}: path through GND copper to nearest GND via",fill=(255,255,255),font=font)
    for i,(t,c) in enumerate([("0 mm",(40,170,60)),("3 mm",(230,220,40)),("6 mm",(240,140,30)),("10 mm",(220,40,40)),(">=20 mm",(120,0,60)),("no path to a via",(60,90,255)),("thinner than 0.6 mm",(255,0,255))]):
        dr.rectangle([lx+(i%4)*150,ly+40+(i//4)*40,lx+(i%4)*150+24,ly+64+(i//4)*40],fill=c); dr.text((lx+(i%4)*150+30,ly+40+(i//4)*40),t,fill=(230,230,230),font=fsm)
    dr.text((lx,ly+125),"Circle yellow/red: SMD GND pad > 1 / > 2 mm from nearest via (edge to edge)",fill=(230,230,230),font=fsm)
    im.save(f"{outdir}/gnd_{L.replace('.','')}.png")
    print(L,'reach max %.1f mm'%np.max(dist[g]),'unreached px',int((gnd&~np.isfinite(dist)).sum()),'thin px',int(thin.sum()))
json.dump(report,open(f"{outdir}/gnd_pads.json",'w'))
