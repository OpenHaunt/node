# Export copper geometry of the outer layers for gnd_map.py. Run with KiCad's bundled python (needs pcbnew).
# Fills zones in memory first, so the result matches a fresh refill; the board file is not modified.
import pcbnew, json, sys
bf,out=sys.argv[1],sys.argv[2]
b=pcbnew.LoadBoard(bf); mm=pcbnew.ToMM
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
LAY={'F.Cu':pcbnew.F_Cu,'B.Cu':pcbnew.B_Cu}
def polys(ps):
    res=[]
    for i in range(ps.OutlineCount()):
        o=ps.Outline(i); ring=[[mm(o.CPoint(k).x),mm(o.CPoint(k).y)] for k in range(o.PointCount())]
        holes=[]
        for h in range(ps.HoleCount(i)):
            hh=ps.Hole(i,h); holes.append([[mm(hh.CPoint(k).x),mm(hh.CPoint(k).y)] for k in range(hh.PointCount())])
        res.append({'o':ring,'h':holes})
    return res
def item_poly(it,L):
    ps=pcbnew.SHAPE_POLY_SET(); it.TransformShapeToPolygon(ps,L,0,pcbnew.FromMM(0.005),pcbnew.ERROR_INSIDE); return polys(ps)
D={'bbox':None,'layers':{}}
bb=b.GetBoardEdgesBoundingBox(); D['bbox']=[mm(bb.GetX()),mm(bb.GetY()),mm(bb.GetRight()),mm(bb.GetBottom())]
gvias=[]
for t in b.GetTracks():
    if t.GetClass()=='PCB_VIA':
        gvias.append({'x':mm(t.GetPosition().x),'y':mm(t.GetPosition().y),'d':mm(t.GetWidth(pcbnew.F_Cu)),'gnd':t.GetNetname()=='GND'})
D['vias']=gvias
for name,L in LAY.items():
    lay={'gnd_zone':[],'gnd_other':[],'other':[],'pads':[]}
    for z in b.Zones():
        if z.GetIsRuleArea() or not z.IsOnLayer(L): continue
        (lay['gnd_zone'] if z.GetNetname()=='GND' else lay['other']).extend(polys(z.GetFilledPolysList(L)))
    for t in b.GetTracks():
        if not t.IsOnLayer(L): continue
        (lay['gnd_other'] if t.GetNetname()=='GND' else lay['other']).extend(item_poly(t,L))
    for f in b.GetFootprints():
        for p in f.Pads():
            if not p.IsOnLayer(L): continue
            pp=item_poly(p,L); g=p.GetNetname()=='GND'
            (lay['gnd_other'] if g else lay['other']).extend(pp)
            if g: lay['pads'].append({'ref':f.GetReference(),'num':p.GetNumber(),'tht':p.GetAttribute()==pcbnew.PAD_ATTRIB_PTH,'x':mm(p.GetPosition().x),'y':mm(p.GetPosition().y),'poly':pp})
    D['layers'][name]=lay
json.dump(D,open(out,'w'))
print('ok',D['bbox'],{k:{kk:len(vv) for kk,vv in v.items()} for k,v in D['layers'].items()})
