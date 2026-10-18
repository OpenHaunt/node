import sys, zipfile, xml.etree.ElementTree as ET, trimesh
# Usage: make_3mf.py <out.3mf> <base.stl> <hood.stl> <pin.stl> <pin count>
# One 3MF per enclosure for PrusaSlicer: every part its own named object, laid out on the
# 360 × 360 Prusa XL bed. The STLs come from `part="base"` etc. and are already oriented.
# PrusaSlicer's own per-object settings (Metadata/Slic3r_PE_model.config) give the pins a brim:
# they stand 18.65 mm tall on a Ø4 end.
BRIM = {"pin": 4}   # object name prefix → brim width in mm
out, base, hood, pin, n_pins = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
GAP = 10
scene = trimesh.Scene()
x = GAP
for name, f in [("base", base), ("hood", hood)]:
    m = trimesh.load(f)
    m.apply_translation([x - m.bounds[0][0], GAP - m.bounds[0][1], -m.bounds[0][2]])
    scene.add_geometry(m, geom_name=name, node_name=name)
    x = m.bounds[1][0] + GAP
for i in range(n_pins):
    m = trimesh.load(pin)
    m.apply_translation([x - m.bounds[0][0], GAP + i * (m.extents[1] + GAP) - m.bounds[0][1], -m.bounds[0][2]])
    scene.add_geometry(m, geom_name=f"pin {i + 1}", node_name=f"pin {i + 1}")
scene.export(out)

ns = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}
with zipfile.ZipFile(out) as z:
    model = ET.fromstring(z.read("3D/3dmodel.model"))
config = ET.Element("config")
for obj in model.iterfind("m:resources/m:object", ns):
    name = obj.get("name")
    brim = next((w for prefix, w in BRIM.items() if name.startswith(prefix)), None)
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
