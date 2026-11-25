"""Build the Furnace Burst sheets and review package from the approved V3 ZIP."""
import argparse
import hashlib
import io
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

import numpy as np
from PIL import Image, ImageDraw

SIZE = 256
EMPTY = Image.new("RGBA", (SIZE, SIZE))
Y, X = np.mgrid[-127.5:128:1, -127.5:128:1]
R = np.hypot(X, Y)
ANGLE = np.arctan2(Y, X)
RINGS = (
    (None, 11, 24, 36, 48, 60, 71, 82, 93, 100, 107, None),
    (None, None, None, None, 18, 32, 50, 70, 89, 98, 105, None),
)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def member(archive, suffix):
    return archive.read(next(n for n in archive.namelist() if n.endswith("/" + suffix)))

def png(data):
    return Image.open(io.BytesIO(data))

def png_bytes(image):
    data = io.BytesIO()
    image.save(data, format="PNG")
    return data.getvalue()

def strip(frames):
    w, h = frames[0].size
    sheet = Image.new("RGBA", (w * len(frames), h))
    for i, frame in enumerate(frames):
        sheet.paste(frame, (i * w, 0))
    return sheet

def cell(sheet, i, columns, w=SIZE, h=SIZE):
    x, y = i % columns * w, i // columns * h
    return sheet.crop((x, y, x + w, y + h))

def rgba(red, green, blue, alpha):
    return Image.fromarray(np.stack(np.broadcast_arrays(red, green, blue, alpha), -1).clip(0, 255).astype("uint8"), "RGBA")

def ring(radius, frame, second):
    if radius is None:
        return EMPTY.copy()
    phase = frame * .15 + second * 1.7
    edge = radius + .51*np.sin(11*ANGLE+phase) + .42*np.sin(23*ANGLE-phase)
    d = np.abs(R-edge)
    width = (1.05 if second else 1.8) * (1.22-frame*.044)
    strength = {1:.66, 3:.98, 5:.96, 7:.96, 9:.37}[frame]
    profile = np.exp(-(d/width)**2)
    glow = np.exp(-(d/(width*2.6))**1.5)
    arc = .79+.21*np.sin(7*ANGLE+phase)**2
    if frame == 9:
        arc *= .30+.70*np.clip(np.cos(5*ANGLE+phase)+.44,0,1)
    return rgba(255, 47+134*profile, 4+51*profile, (180*profile+70*glow)*strength*arc)

def flame_shards(frame, ember):
    # Fixed trajectories keep recognizable particles across the generated cuts.
    layer = Image.new("RGBA", (512,512))
    draw = ImageDraw.Draw(layer)
    for i in range(15 if ember else 9):
        angle = i*2.39996 + frame*.036
        progress = max(0, frame - 1 - i%4)
        if progress == 0:
            continue
        fade = max(0, 1-progress/(10+i%3))
        radius = 5 + progress*(4.4 if ember else 2.7) + i%4
        length = (2+i%4 if ember else 9+i%13)*(.55+.45*fade)
        x, y = np.cos(angle), np.sin(angle)
        px, py = 128+radius*x, 128+radius*y
        width = (.48 if ember else 1.4)*fade
        points = [(px+x*length,py+y*length),(px-y*width,py+x*width),
                  (px-x*length*.4,py-y*length*.4),(px+y*width,py-x*width)]
        draw.polygon([(2*a,2*b) for a,b in points], fill=(255,135+i%4*23,25,round(142*fade)))
    return layer.resize((256,256), Image.Resampling.LANCZOS)

def middle(layers, name, frame):
    if frame == 11:
        return EMPTY.copy()
    if name.endswith("shockwave"):
        k = int(name.startswith("secondary"))
        return ring(RINGS[k][frame],frame,k)
    if name == "overlap_flash":
        if frame != 9:
            return EMPTY.copy()
        ridge = np.exp(-((R-99)/1.8)**2)
        arc = np.clip((np.cos(3*ANGLE+.6)-.92)*12,0,1)**2
        return rgba(255,171,61,58*ridge*arc)
    left, right = layers[frame//2:frame//2+2]
    # Carry approved structures from each side along separate local motions.
    left = left.rotate(3 if name=="ember" else 2, Image.Resampling.BICUBIC)
    right = right.rotate(-3 if name=="ember" else -2, Image.Resampling.BICUBIC)
    result = Image.blend(left.convert("RGBa"), right.convert("RGBa"), .5).convert("RGBA")
    if name in ("ember", "short_flame_burst"):
        result.alpha_composite(flame_shards(frame,name=="ember"))
    return result

def projectile(frame):
    y,x=np.mgrid[-47.5:48:1,-79.5:80:1]
    phase=frame*np.pi/4
    sph=np.hypot((x-15)/1.15,y)
    contour=22+1.3*np.sin(np.arctan2(y,x-15)*5+phase)
    shell=np.exp(-((sph-contour)/2.7)**2)
    glow=np.exp(-((sph-contour)/5)**2)
    wake=np.clip((32-x)/18,0,1)*(x>-53)
    tail=np.exp(-((y-1.3*np.sin(x*.13+phase))/np.maximum(1.5,10+(x+25)*.11))**2)
    taper=np.clip((x+55)/60,0,1)*wake
    outer=rgba(255,58+90*shell,9+15*shell,np.maximum(130*shell+80*glow,200*tail*taper))
    hot=np.exp(-(sph/(15+.8*np.sin(phase)))**3)
    knot=np.exp(-(((x-20-2*np.sin(phase))/11)**2+((y-2*np.cos(phase))/9)**2))
    core=rgba(255,132+114*knot,12+212*knot,246*hot)
    ember=Image.new("RGBA",(160,96))
    pen=ImageDraw.Draw(ember)
    for i in range(6):
        t=(frame/8+i*.174)%1
        sx=70-46*t
        sy=48+(-1)**i*(4+i*1.6)*t
        a=int(210*np.sin(np.pi*t)**1.5)
        pen.line((sx-2,sy,sx+1,sy),fill=(255,124+i*14,35,a))
    return [outer,core,ember]

def composite(layers):
    result=Image.new("RGBA",layers[0].size)
    for image in layers:
        result.alpha_composite(image)
    return result

def apng(path, frames, fps, loops):
    frames[0].save(path,format="PNG",save_all=True,append_images=frames[1:],
                   duration=[1000/fps]*len(frames),loop=loops,disposal=0,blend=0)
    with Image.open(path) as preview:
        assert preview.n_frames==len(frames)
        for i, frame in enumerate(frames):
            preview.seek(i)
            assert preview.tobytes()==frame.tobytes()
            assert preview.info["duration"]==1000/fps

def clean_edges(frames):
    for frame in frames:
        assert frame.mode=="RGBA" and frame.getchannel("A").getextrema()[0]==0
        bounds=frame.getbbox()
        if bounds:
            x0,y0,x1,y1=bounds
            assert x0>0 and y0>0 and x1<frame.width and y1<frame.height,bounds

def contact(path, rows):
    thumb=100
    out=Image.new("RGB",(112+len(rows[0][1])*thumb,len(rows)*110),(30,28,34))
    draw=ImageDraw.Draw(out)
    for j,(label,frames) in enumerate(rows):
        draw.text((5,j*110+45),label,fill="white")
        for i,frame in enumerate(frames):
            view=frame.resize((thumb,thumb),Image.Resampling.LANCZOS) if frame.width==frame.height else frame.resize((thumb,60),Image.Resampling.LANCZOS)
            out.paste(view,(112+i*thumb,j*110+(100-view.height)//2),view)
            draw.text((112+i*thumb,j*110+98),f"{i+1:02d}",fill="white")
    out.save(path)

def package(path, sheets, preview, center, fps):
    with ZipFile(path,"w",ZIP_DEFLATED) as z:
        for name,frames in sheets.items():
            z.writestr(f"layers/{name}.png" if name!="combined" else "combined.png",png_bytes(strip(frames)))
        z.write(preview,"preview.apng")
        z.writestr("metadata.json",json.dumps({"fps":fps,"frames":len(sheets["combined"]),
                    "anchor":center,"cell":sheets["combined"][0].size,"layout":"single row","loop":fps==16},indent=2))

def main():
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument("v3_zip",type=Path)
    cli.add_argument("game_assets",type=Path)
    cli.add_argument("review",type=Path)
    options=cli.parse_args()
    out=options.review
    out.mkdir(parents=True,exist_ok=True)
    with ZipFile(options.v3_zip) as z:
        source_hash=digest(options.v3_zip.read_bytes())
        assert source_hash=="91d9b1144dca7ac4f35355c9426c61369fbe120ccd08e616ca6b241301be6738"
        metadata=json.loads(member(z,"combined/metadata.json"))
        assert metadata["target_anchor_px"]==[128,128] and metadata["cell_size_px"]==[256,256]
        order=metadata["layer_order_bottom_to_top"]
        def keys(name):
            sheet=png(member(z,name))
            assert sheet.mode=="RGBA" and sheet.size==(768,512)
            return [cell(sheet,i,3) for i in range(6)]
        original=keys("combined/explosion_keyframes_6f.png")
        approved_hashes=[]
        for i,frame in enumerate(original):
            data=member(z,f"frames/K{i+1:02d}.png")
            assert png(data).tobytes()==frame.tobytes()
            approved_hashes.append(digest(data))
        layers={}
        for name in order:
            frames=keys(f"layers/{name}_6f.png")
            layers[name]=[frames[f//2] if f%2==0 else middle(frames,name,f) for f in range(12)]
    boom=[composite(row) for row in zip(*layers.values())]
    assert all(boom[i*2].tobytes()==key.tobytes() for i,key in enumerate(original))
    assert boom[-1].getbbox() is None
    bolt_layers=dict(zip(("energy_shell","core","ember"),map(list,zip(*(projectile(f) for f in range(8))))))
    bolt=[composite(row) for row in zip(*bolt_layers.values())]
    for name,frames in (("explosion",boom),("round",bolt)):
        clean_edges(frames)
        assert len({digest(f.tobytes()) for f in frames})==len(frames)
    for frames in (*layers.values(), *bolt_layers.values()):
        clean_edges(frames)
    # Measure the radial ridge and its opposite halves from the actual alpha data.
    measured=[]
    bins=np.floor(R+.5).astype(int)
    for name,expected in zip(order[:2],RINGS):
        row=[]
        for frame,want in zip(layers[name],expected):
            alpha=np.asarray(frame.getchannel("A"))
            if want is None:
                assert not alpha.any()
                row.append(None)
                continue
            radii=[]
            for side in (X<0,X>=0,Y<0,Y>=0):
                strength=np.bincount(bins[side],alpha[side])/np.maximum(1,np.bincount(bins[side]))
                radii.append(int(np.argmax(strength)))
            assert max(abs(r-want) for r in radii)<=2,(name,want,radii)
            row.append(round(sum(radii)/4,2))
        measured.append(row)
    primary,secondary=measured
    gaps=[a-b for a,b in zip(primary,secondary) if a is not None and b is not None]
    assert all(a>=b>=0 for a,b in zip(gaps,gaps[1:])) and 0<=gaps[4]<=6
    assert secondary[8]-secondary[4] > primary[8]-primary[4]
    mean_jumps=[round(float(np.mean(np.abs(np.asarray(a.convert("RGBa"),dtype=float)-np.asarray(b.convert("RGBa"),dtype=float)))),3) for a,b in zip(boom,boom[1:])]
    # Bright, large rings change many pixels as they travel. Report the delta for
    # visual review; enforce motion continuity using the measured ring ridges.
    for row in measured:
        radial_steps = [b - a for a, b in zip(row, row[1:]) if a is not None and b is not None]
        assert all(0 < step <= 22 for step in radial_steps), radial_steps
    for name, frames in (("explosion", boom), ("round", bolt)):
        strip(frames).save(options.game_assets / f"fx_furnace_{name}.png")
    chart=Image.new("RGB",(590,330),(30,28,34))
    draw=ImageDraw.Draw(chart)
    draw.text((20,15),"Shockwave ridge radius / cell px",fill="white")
    for row,color in zip(measured,("#ff952f","#ffda74")):
        points=[(48+45*i,285-2.1*r) for i,r in enumerate(row) if r is not None]
        draw.line(points,fill=color,width=3)
        for x,y in points:
            draw.ellipse((x-3,y-3,x+3,y+3),fill=color)
    for i in range(12):
        draw.text((42+45*i,295),f"{i+1:02d}",fill="white")
    draw.text((320,15),"primary",fill="#ff952f")
    draw.text((410,15),"secondary",fill="#ffda74")
    chart.save(out/"shockwave-radii.png")
    apng(out/"explosion.apng",boom,20,1)
    apng(out/"projectile.apng",bolt,16,0)
    contact(out/"explosion-layers.png",[("combined",boom),*layers.items()])
    contact(out/"projectile-layers.png",[("combined",bolt),*bolt_layers.items()])
    package(out/"STEEL_DRAGON_A_SKILL01_EXPLOSION_12F_V1.zip",{**layers,"combined":boom},out/"explosion.apng",[128,128],20)
    package(out/"STEEL_DRAGON_A_SKILL01_PROJECTILE_V1.zip",{**bolt_layers,"combined":bolt},out/"projectile.apng",[80,48],16)
    (out/"asset-qa.json").write_text(json.dumps({"source_sha256":source_hash,"key_png_sha256":approved_hashes,
                       "approved_key_rgba_differences":[0]*6,"measured_radii":measured,"radial_gaps":gaps,"premultiplied_rgba_mean_step_delta":mean_jumps},indent=2))
    print("Approved pixels match all six fixed keyframes; radius ridges:", measured)

if __name__=="__main__":
    main()
