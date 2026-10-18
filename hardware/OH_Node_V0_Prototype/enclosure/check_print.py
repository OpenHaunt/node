import sys, numpy as np, trimesh
# Usage: check_print.py <part.stl | plate.3mf>...
# Printability on a Prusa XL: each part one solid body (nothing floating or joined only by a
# face or an edge), closed, lying on the bed at z = 0 and inside the 360 × 360 × 360 volume.
# A 3MF is a plate: each of its objects is checked, and the plate must fit the bed.
BED = (360, 360, 360)
fail = False

def parts(f):
    global fail
    loaded = trimesh.load(f, process=True)
    if not isinstance(loaded, trimesh.Scene):
        yield f, loaded
        return
    plate = loaded.dump(concatenate=True)
    if any(plate.bounds[1][i] > BED[i] or plate.bounds[0][i] < 0 for i in range(3)):
        print(f"{f}: FAIL plate {np.round(plate.bounds, 1).tolist()} is not on the bed")
        fail = True
    for node in loaded.graph.nodes_geometry:
        T, g = loaded.graph[node]
        yield f"{f} [{node}]", loaded.geometry[g].copy().apply_transform(T)

for path in sys.argv[1:]:
    for f, m in parts(path):
        bodies = m.split(only_watertight=False)
        e = np.sort(m.edges, axis=1)
        _, n = np.unique(e, axis=0, return_counts=True)
        problems = []
        if len(bodies) != 1:
            problems.append(f"{len(bodies)} bodies: " + ", ".join(
                f"{b.volume:.1f} mm³ at {np.round(b.bounds[0], 1).tolist()}" for b in sorted(bodies, key=lambda b: b.volume)[:-1]))
        if not m.is_watertight: problems.append("not watertight")
        if (n > 2).sum(): problems.append(f"{(n > 2).sum()} non-manifold edges")
        if abs(m.bounds[0][2]) > 0.01: problems.append(f"lowest point at z = {m.bounds[0][2]:.2f}, not on the bed")
        if any(m.extents[i] > BED[i] for i in range(3)): problems.append(f"extent {np.round(m.extents, 1).tolist()} exceeds the bed")
        on_bed = m.area_faces[(m.face_normals[:, 2] < -0.999) & (m.triangles_center[:, 2] < 0.01)].sum()
        print(f"{f}: {m.volume:.0f} mm³, {np.round(m.extents, 1).tolist()} mm, {on_bed:.0f} mm² on the bed"
              + ("" if not problems else "\n  FAIL " + "\n  FAIL ".join(problems)))
        fail |= bool(problems)
sys.exit(1 if fail else 0)
