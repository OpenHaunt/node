import os, sys, zipfile, xml.etree.ElementTree as ET, trimesh
# Usage: make_3mf.py <out.3mf> <base.stl> <hood.stl> <pin.stl>[,<pin.stl>...] [<base.stl> <hood.stl> <pins>]...
# One 3MF for PrusaSlicer: every part its own named object, laid out on the 360 × 360 Prusa XL
# bed, one row per enclosure. The STLs come from `part="base"` etc. and are already oriented.
# Objects are named after the STL file: base.stl gives "base", "v0.1 base.stl" gives "v0.1 base".
# PrusaSlicer's own per-object settings (Metadata/Slic3r_PE_model.config) give the pins a brim:
# they stand about 20 mm tall on a Ø4 end.
BRIM = {"pin": 4}   # part → brim width in mm
out, args = sys.argv[1], sys.argv[2:]
assert args and len(args) % 3 == 0, "arguments come in groups of <base.stl> <hood.stl> <pin.stl>[,<pin.stl>...]"
name = lambda f: os.path.splitext(os.path.basename(f))[0]
GAP = 10
scene = trimesh.Scene()
kind = {}   # object name → base, hood or pin
y = GAP
for base, hood, pins in zip(args[0::3], args[1::3], args[2::3]):
    x = GAP
    row = y
    for k, f in [("base", base), ("hood", hood)]:
        m = trimesh.load(f)
        m.apply_translation([x - m.bounds[0][0], row - m.bounds[0][1], -m.bounds[0][2]])
        scene.add_geometry(m, geom_name=name(f), node_name=name(f))
        kind[name(f)] = k
        x = m.bounds[1][0] + GAP
        y = max(y, m.bounds[1][1] + GAP)
    py = row
    for pin in pins.split(","):
        m = trimesh.load(pin)
        m.apply_translation([x - m.bounds[0][0], py - m.bounds[0][1], -m.bounds[0][2]])
        n = name(pin)
        scene.add_geometry(m, geom_name=n, node_name=n)
        py = m.bounds[1][1] + GAP
        kind[n] = "pin"
        y = max(y, m.bounds[1][1] + GAP)
scene.export(out)

ns = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}
with zipfile.ZipFile(out) as z:
    model = ET.fromstring(z.read("3D/3dmodel.model"))
config = ET.Element("config")
for obj in model.iterfind("m:resources/m:object", ns):
    name = obj.get("name")
    brim = BRIM.get(kind.get(name))
    if brim is None: continue
    n = len(obj.findall("m:mesh/m:triangles/m:triangle", ns))
    o = ET.SubElement(config, "object", id=obj.get("id"), instances_count="1")
    ET.SubElement(o, "metadata", type="object", key="name", value=name)
    ET.SubElement(o, "metadata", type="object", key="brim_type", value="outer_only")
    ET.SubElement(o, "metadata", type="object", key="brim_width", value=str(brim))
    v = ET.SubElement(o, "volume", firstid="0", lastid=str(n - 1))
    ET.SubElement(v, "metadata", type="volume", key="name", value=name)
with zipfile.ZipFile(out, "a", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Metadata/Slic3r_PE_model.config", ET.tostring(config, encoding="unicode", xml_declaration=True))
print(out, "plate", [round(v, 1) for v in scene.extents])
