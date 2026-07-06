import struct, json, math, os

# ---- geometry: a 4-segment square column (5 rings) that bends via 4 bones ----
H = 0.3                      # cross-section half-width
RINGS = 5                    # y = 0..4
NBONES = 4                   # bone i sits at y=i (i=0..3); top ring shares bone 3
corners = [( H, H), ( H,-H), (-H,-H), (-H, H)]

positions, normals, uvs, joints, weights = [], [], [], [], []
for r in range(RINGS):
    y = float(r)
    for ci,(x,z) in enumerate(corners):
        positions.append((x, y, z))
        n = (x, 0.0, z); L = math.hypot(x, z) or 1.0
        normals.append((n[0]/L, 0.0, n[2]/L))
        uvs.append((ci/4.0, y/(RINGS-1)))
        b = min(r, NBONES-1)
        joints.append((b,0,0,0))
        weights.append((1.0,0.0,0.0,0.0))

indices = []
def vid(r,c): return r*4 + c
for s in range(RINGS-1):              # side quads
    for c in range(4):
        a=vid(s,c); b=vid(s,(c+1)%4); d=vid(s+1,(c+1)%4); e=vid(s+1,c)
        indices += [a,b,d, a,d,e]
nverts=len(positions); nidx=len(indices)

# ---- animation: each bone waves about Z, phase-shifted -> travelling wave ----
DUR=2.0; NK=33
times=[DUR*k/(NK-1) for k in range(NK)]
def quatZ(a): return (0.0,0.0,math.sin(a/2),math.cos(a/2))   # glTF quat order x,y,z,w
def quatY(a): return (0.0,math.sin(a/2),0.0,math.cos(a/2))
bone_quats=[]; bone_quats_tw=[]
for i in range(NBONES):
    qw=[]; qt=[]
    for t in times:
        qw.append(quatZ(0.30*math.sin(2*math.pi*(t/DUR) - i*0.7)))   # Wave: bend about Z
        qt.append(quatY(0.45*math.sin(2*math.pi*(t/DUR) - i*1.2)))   # Twist: twist about Y
    bone_quats.append(qw); bone_quats_tw.append(qt)

# inverse bind: bone i global bind = translate(0,i,0) -> inverse translate(0,-i,0)
def ibm(i):  # column-major mat4
    return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-float(i),0,1]

# ---- pack one binary blob; record (offset,length) per view (4-byte aligned) ----
blob=bytearray(); views=[]
def add(data_bytes):
    while len(blob)%4: blob.append(0)
    off=len(blob); blob.extend(data_bytes); views.append((off,len(data_bytes)))
    return len(views)-1

v_pos=add(b''.join(struct.pack('<3f',*p) for p in positions))
v_nrm=add(b''.join(struct.pack('<3f',*n) for n in normals))
v_uv =add(b''.join(struct.pack('<2f',*u) for u in uvs))
v_jnt=add(b''.join(struct.pack('<4B',*j) for j in joints))
v_wgt=add(b''.join(struct.pack('<4f',*w) for w in weights))
v_idx=add(b''.join(struct.pack('<H',i) for i in indices))
v_ibm=add(b''.join(struct.pack('<16f',*ibm(i)) for i in range(NBONES)))
v_time=add(b''.join(struct.pack('<f',t) for t in times))
v_bone=[add(b''.join(struct.pack('<4f',*q) for q in bone_quats[i])) for i in range(NBONES)]
v_bone_tw=[add(b''.join(struct.pack('<4f',*q) for q in bone_quats_tw[i])) for i in range(NBONES)]

def bv(i): return {"buffer":0,"byteOffset":views[i][0],"byteLength":views[i][1]}
pmin=[min(p[k] for p in positions) for k in range(3)]
pmax=[max(p[k] for p in positions) for k in range(3)]

accessors=[
 {"bufferView":v_pos,"componentType":5126,"count":nverts,"type":"VEC3","min":pmin,"max":pmax}, #0 POSITION
 {"bufferView":v_nrm,"componentType":5126,"count":nverts,"type":"VEC3"},                       #1 NORMAL
 {"bufferView":v_uv, "componentType":5126,"count":nverts,"type":"VEC2"},                       #2 UV
 {"bufferView":v_jnt,"componentType":5121,"count":nverts,"type":"VEC4"},                       #3 JOINTS
 {"bufferView":v_wgt,"componentType":5126,"count":nverts,"type":"VEC4"},                       #4 WEIGHTS
 {"bufferView":v_idx,"componentType":5123,"count":nidx,"type":"SCALAR"},                       #5 indices
 {"bufferView":v_ibm,"componentType":5126,"count":NBONES,"type":"MAT4"},                       #6 IBM
 {"bufferView":v_time,"componentType":5126,"count":NK,"type":"SCALAR","min":[times[0]],"max":[times[-1]]}, #7 times
]
acc_bone=[]; acc_bone_tw=[]
for i in range(NBONES):
    accessors.append({"bufferView":v_bone[i],"componentType":5126,"count":NK,"type":"VEC4"})
    acc_bone.append(len(accessors)-1)
for i in range(NBONES):
    accessors.append({"bufferView":v_bone_tw[i],"componentType":5126,"count":NK,"type":"VEC4"})
    acc_bone_tw.append(len(accessors)-1)

# nodes: 0..3 bones (chain), 4 mesh(skin)
nodes=[
 {"name":"bone0","translation":[0,0,0],"children":[1]},
 {"name":"bone1","translation":[0,1,0],"children":[2]},
 {"name":"bone2","translation":[0,1,0],"children":[3]},
 {"name":"bone3","translation":[0,1,0]},
 {"name":"Column","mesh":0,"skin":0},
]
skins=[{"joints":[0,1,2,3],"skeleton":0,"inverseBindMatrices":6}]
meshes=[{"name":"Column","primitives":[{
  "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":4},
  "indices":5,"material":0}]}]
materials=[{"name":"Skin","pbrMetallicRoughness":{"baseColorFactor":[0.85,0.55,0.4,1.0],"metallicFactor":0.0,"roughnessFactor":0.7}}]
samplers=[{"input":7,"output":acc_bone[i],"interpolation":"LINEAR"} for i in range(NBONES)]
channels=[{"sampler":i,"target":{"node":i,"path":"rotation"}} for i in range(NBONES)]
samplers_tw=[{"input":7,"output":acc_bone_tw[i],"interpolation":"LINEAR"} for i in range(NBONES)]
channels_tw=[{"sampler":i,"target":{"node":i,"path":"rotation"}} for i in range(NBONES)]
animations=[{"name":"Wave","samplers":samplers,"channels":channels},
            {"name":"Twist","samplers":samplers_tw,"channels":channels_tw}]

gltf={"asset":{"version":"2.0","generator":"GameEngine gen_character"},
 "scene":0,"scenes":[{"nodes":[0,4]}],"nodes":nodes,"skins":skins,
 "meshes":meshes,"materials":materials,"animations":animations,
 "accessors":accessors,"bufferViews":[bv(i) for i in range(len(views))],
 "buffers":[{"byteLength":len(blob)}]}

# ---- write .glb (12B header + JSON chunk + BIN chunk, each 4-aligned) ----
jsonb=json.dumps(gltf,separators=(',',':')).encode('utf-8')
while len(jsonb)%4: jsonb+=b' '
binb=bytes(blob)
while len(binb)%4: binb+=b'\x00'
out="character/assets/models/character.glb"
with open(out,"wb") as f:
    total=12+8+len(jsonb)+8+len(binb)
    f.write(struct.pack('<III',0x46546C67,2,total))
    f.write(struct.pack('<II',len(jsonb),0x4E4F534A)); f.write(jsonb)
    f.write(struct.pack('<II',len(binb),0x004E4942)); f.write(binb)
print(f"wrote {out}: {os.path.getsize(out)} bytes | {nverts} verts, {nidx//3} tris, {NBONES} bones, {NK} keys, anim '{animations[0]['name']}' {DUR}s")
