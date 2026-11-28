"""Generate Core Overheat FX, pose-matched armor masks and review assets."""
import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from build_steel_furnace_assets import apng, cell, clean_edges, composite, rgba, strip

CELL = 384
y, x = np.mgrid[-191.5:192, -191.5:192]
radius = np.hypot(x, y)
angle = np.arctan2(y, x)
radii = [6, 23, 41, 58, 74, 89, 103, 116, 128, 139, 148, 155, 161, 165, 168, 172]
poses = {"idle": "idle", "move": "move", "fly": "fly", "attack": "attack",
         "idle_vent": "fx_idle_vent_body"}

def release():
    rng = np.random.default_rng(1182)
    sparks = rng.uniform(0, 1, (72, 6))
    flames = rng.uniform(0, 1, (28, 5))
    rows = {k: [] for k in ("core", "flame", "shockwave", "ember")}
    for i, ring_radius in enumerate(radii):
        field = np.zeros_like(radius)
        for index, (phase, birth, life, velocity, bend) in enumerate(flames):
            age = i - 4 * birth + 1
            span = 7 + 5 * life
            if not 0 < age < span:
                continue
            heading = index * 2.39996 + phase * .45 + bend * .032 * age
            u = x * np.cos(heading) + y * np.sin(heading)
            v = -x * np.sin(heading) + y * np.cos(heading)
            length = 18 + 6 * age
            center = age * (5 + 7 * velocity)
            body = np.exp(-((u-center)/length)**2 - ((v - 5*np.sin(u*.085+phase*5))/(4 + age*1.7))**2)
            envelope = min(1, age*.53) * (1-age/span)**.82
            field += body * envelope
        noise = .72 + .28*np.sin(x*.29+y*.39+i*.55)*np.cos(y*.21-i*.41)
        heat = np.clip(field * noise, 0, 3)
        core = np.exp(-(radius/(15 + min(i,4)*4))**2)*np.exp(-i*.42)
        rows["core"].append(rgba(255, 202+51*core, 76+165*core, 245*core))
        rows["flame"].append(rgba(255, 41+170*np.clip(heat*.76,0,1),
                                   4+152*np.clip((heat-.35)*.8,0,1), np.clip(heat*218,0,249)*np.clip((188-radius)/14,0,1)*np.clip((11-i)/5,0,1)))
        ridge = np.exp(-((radius-ring_radius)/(3.1-0.125*i))**2)
        halo = np.exp(-((radius-ring_radius)/(8.0-.33*i))**2)
        exposure = (1-i/16)**1.12
        rows["shockwave"].append(rgba(255, 76+107*ridge, 11+75*ridge, exposure*(190*ridge+61*halo)))
        spark = Image.new("RGBA",(CELL,CELL))
        draw = ImageDraw.Draw(spark)
        for phase,birth,life,speed,brightness,bend in sparks:
            age = i - 7*birth
            span = 5 + 8*life
            if not 0 < age < span:
                continue
            a = phase*2*np.pi + age*bend*.023
            vx,vy = np.cos(a),np.sin(a)
            distance = 12 + age*(5+8*speed)
            px,py = 192+distance*vx,192+distance*vy
            tail = 3+7*brightness
            strength = min(1,age*.8)*(1-age/span)
            draw.line((px-vx*tail,py-vy*tail,px,py),
                      fill=(255,round(75+135*strength),round(12+75*strength),round(232*strength)),
                      width=1+int(brightness>.7))
        rows["ember"].append(spark)
    return rows

def armor_heat(frame):
    data = np.asarray(frame,dtype=float)/255
    red,green,blue,source_alpha = np.moveaxis(data,-1,0)
    # Warm seams are found in each pose. A worn-metal falloff spreads their heat
    # locally while preserving the source's plate highlights and shadow contrast.
    warmth = np.clip((red-green)*2.2 + (green-blue)*.6,0,1)
    seam = np.clip((red-.24)*1.9,0,1)*warmth*(source_alpha>.65)
    spread = np.asarray(Image.fromarray(np.uint8(seam*255)).filter(ImageFilter.GaussianBlur(7)))/255
    luminance = .21*red+.71*green+.08*blue
    yy,xx = np.mgrid[:256,:256]
    zone = np.exp(-((xx-163)/85)**2-((yy-166)/80)**2)
    steel = 1-np.clip((red-blue)*1.8,0,1)
    plate_heat = (spread * 3.6 + .42 * zone * np.clip(luminance * 2.5,0,1)) * steel
    alpha = np.clip((plate_heat + seam*.54) * (.24+.76*zone) * (.45 + .55*luminance),0,.72)
    return rgba(84+176*luminance+55*seam, 6+30*luminance+103*seam,
                6+25*luminance+14*seam, 255*alpha*source_alpha)

def main():
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument("assets",type=Path)
    cli.add_argument("review",type=Path)
    args=cli.parse_args()
    out=args.review
    out.mkdir(parents=True,exist_ok=True)
    layers=release()
    front=[composite([flame,core,ember]) for flame,core,ember in
           zip(layers["flame"],layers["core"],layers["ember"])]
    rear=layers["shockwave"]
    all_frames=[composite([back,fore]) for back,fore in zip(rear,front)]
    for row in [*layers.values(),front,all_frames]:
        assert len(row)==16
        clean_edges(row)
    measured=[]
    bins=np.floor(radius+.5).astype(int)
    for frame in rear:
        alpha=np.asarray(frame.getchannel("A"))
        measured.append(int(np.argmax(np.bincount(bins.ravel(),alpha.ravel()) /
                                      np.maximum(1,np.bincount(bins.ravel())))))
    assert all(abs(a-b)<=1 for a,b in zip(measured,radii))
    assert all(a<b for a,b in zip(measured,measured[1:]))
    masks,og,digests={},{},{}
    for mode,name in poses.items():
        path=args.assets/(name+".png")
        original=Image.open(path)
        assert original.mode=="RGBA" and original.height==256 and original.width%256==0
        digests[name]=hashlib.sha256(path.read_bytes()).hexdigest()
        frames=[cell(original,i,original.width//256) for i in range(original.width//256)]
        og[mode]=frames
        masks[mode]=[armor_heat(frame) for frame in frames]
        for heat,body in zip(masks[mode],frames):
            h=np.asarray(heat.getchannel("A"))
            b=np.asarray(body.getchannel("A"))
            assert h.max()>0 and np.all(h<=b)
        strip(masks[mode]).save(args.assets/f"fx_overheat_{mode}.png")
    strip(front).save(args.assets/"fx_overheat_front.png")
    strip(rear).save(args.assets/"fx_overheat_rear.png")
    apng(out/"explosion.apng",all_frames,20,1)
    motion=[]
    for i in range(24):
        panel=Image.new("RGBA",(768,256))
        for j,mode in enumerate(("idle","move","fly")):
            n=i%len(og[mode])
            alpha=min(1,(i+1)/6) if i<8 else max(0,(24-i)/16)
            heat=masks[mode][n].copy()
            heat.putalpha(heat.getchannel("A").point(lambda a: round(a*alpha)))
            work=composite([og[mode][n],heat])
            panel.paste(work,(j*256,0))
        motion.append(panel)
    apng(out/"armor-idle-walk-fly.apng",motion,20,0)
    meta={"explosion":{"fps":20,"frames":16,"duration":.8,"loop":False,"cell":[384,384],"center":[192,192]},
          "ring_ridge_px":measured,"armor_source_sha256":digests,"armor_counts":{n:len(v) for n,v in masks.items()}}
    (out/"asset-qa.json").write_text(json.dumps(meta,indent=2),encoding="utf-8")
    with ZipFile(out/"STEEL_DRAGON_A_SKILL02_CORE_OVERHEAT_V1.zip","w",ZIP_DEFLATED) as archive:
        for name,images in {**layers,"combined":all_frames}.items():
            path=out/(name+".png")
            strip(images).save(path)
            archive.write(path,"explosion/"+path.name)
        for name in masks:
            path=args.assets/f"fx_overheat_{name}.png"
            archive.write(path,"armor/"+path.name)
        for name in ("explosion.apng","armor-idle-walk-fly.apng","asset-qa.json"):
            archive.write(out/name,name)
        archive.writestr("README.md","Core Overheat — 16 RGBA frames at 20 FPS. Center (192,192).\n"
                     "Armor sheets share the original pose's frame indices and silhouettes.\n"
                     "Apply a ramp for windup and fade out the same masks during cooling.\n")
    print(json.dumps(meta,indent=2))

if __name__=="__main__":
    main()
