"""Convert spine_01.fbx (the-human-spinal-column.zip) into a web-ready GLB.

Usage:
  Blender -b --factory-startup -P scripts/export_spine_glb.py -- <input.fbx> <output.glb>
"""
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
src = argv[0] if len(argv) > 0 else "spine_01.fbx"
out = argv[1] if len(argv) > 1 else "assets/models/human_spine.glb"

LEVELS = ["Atlas", "Axis", "C03", "C04", "C05", "C06", "C07"]
LEVELS += ["T%02d" % i for i in range(1, 13)]
LEVELS += ["L%02d" % i for i in range(1, 6)]
LEVELS += ["Sacrum"]
NICE = {"Atlas": "C1", "Axis": "C2", "Sacrum": "SACRUM"}
DISC_SRC = "Male_Skeletal_Intervertabral_Discs_Geo"


def level_name(level):
    return NICE[level] if level in NICE else level[0] + str(int(level[1:]))


def center(obj):
    return sum((obj.matrix_world @ Vector(c) for c in obj.bound_box), Vector()) / 8


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)

keep = {"Male_Skeletal_%s_Geo" % level: level_name(level) for level in LEVELS}
for obj in list(bpy.data.objects):
    if obj.type != "MESH" or (obj.name not in keep and obj.name != DISC_SRC):
        bpy.data.objects.remove(obj, do_unlink=True)

# The FBX "Back" group carries a broken non-uniform scale, so drop parent
# transforms; all spine meshes share a consistent local Z-up space.
for obj in bpy.data.objects:
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
bpy.ops.object.parent_clear(type="CLEAR")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

disc = bpy.data.objects[DISC_SRC]
bpy.ops.object.select_all(action="DESELECT")
disc.select_set(True)
bpy.context.view_layer.objects.active = disc
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.separate(type="LOOSE")
bpy.ops.object.mode_set(mode="OBJECT")

order = [level_name(level) for level in LEVELS]
pairs = [a + "-" + (b if b != "SACRUM" else "S1") for a, b in zip(order[1:], order[2:])]
parts = sorted((o for o in bpy.data.objects if o.name.startswith(DISC_SRC)), key=lambda o: -center(o).z)
if len(parts) != len(pairs):
    raise RuntimeError("Expected %d discs, found %d" % (len(pairs), len(parts)))
for part, pair in zip(parts, pairs):
    part.name = "DISC_" + pair
for source, name in keep.items():
    bpy.data.objects[source].name = name

bpy.ops.object.select_all(action="DESELECT")
for obj in list(bpy.data.objects):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    modifier = obj.modifiers.new(name="dec", type="DECIMATE")
    modifier.ratio = 0.6 if obj.name.startswith("DISC_") else 0.35
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)
    for poly in obj.data.polygons:
        poly.use_smooth = True
    obj.data.materials.clear()
    for uv in list(obj.data.uv_layers):
        obj.data.uv_layers.remove(uv)

bpy.ops.export_scene.gltf(
    filepath=out,
    export_format="GLB",
    export_materials="NONE",
    export_normals=True,
    export_texcoords=False,
    export_apply=True,
)
