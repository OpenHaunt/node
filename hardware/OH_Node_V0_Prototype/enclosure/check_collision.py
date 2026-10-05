import sys, numpy as np, trimesh, manifold3d as m3
# Usage: check_collision.py <dir with base_placed.stl, hood_placed.stl, pin_placed.stl> <pcb.stl>
sp = sys.argv[1]; pcb = sys.argv[2]
mesh = trimesh.load(pcb, process=True)
shells = mesh.split(only_watertight=False)
print("pcb shells:", len(shells))
def to_m(t):
    return m3.Manifold(m3.Mesh(vert_properties=np.asarray(t.vertices, np.float32), tri_verts=np.asarray(t.faces, np.uint32)))
parts = {p: to_m(trimesh.load(f"{sp}/{p}_placed.stl")) for p in ("base", "hood", "pin")}
hits = []
for s in shells:
    if len(s.vertices) < 4: continue
    try: h = s.convex_hull
    except Exception: continue
    hm = to_m(h)
    for name, pm in parts.items():
        v = (hm ^ pm).volume()
        if v > 0.01:
            b = np.round(h.bounds, 2)
            hits.append((name, round(v, 3), b[0].tolist(), b[1].tolist()))
for hit in sorted(hits, key=lambda x: -x[1]): print(hit)
print("hits:", len(hits))
