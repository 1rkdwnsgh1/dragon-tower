from PIL import Image, ImageDraw
import os
D = "assets/ui/default"
BORDER_SHARE, MIN_SCALE = 0.35, 0.55

def load_cfg():
    cfg, sec = {}, None
    for line in open(f"{D}/ui.txt", encoding="utf-8"):
        line = line.split(";")[0].strip()
        if not line: continue
        if line.startswith("[") and line.endswith("]"):
            sec = line[1:-1]; cfg.setdefault(sec, {"fit": "nine", "left":0,"top":0,"right":0,"bottom":0}); continue
        if "=" in line and sec:
            k, v = line.split("=", 1)
            cfg[sec][k] = v if k == "fit" else int(v)
    return cfg
CFG = load_cfg()

def draw_piece(canvas, name, box):
    """C++ drawPiece 와 같은 계산. 그렸으면 content 사각형, 못 그렸으면 None."""
    path = f"{D}/{name}.png"
    if not os.path.exists(path): return None
    im = Image.open(path).convert("RGBA")
    sw, sh = im.size
    c = CFG.get(name, {"fit":"nine","left":0,"top":0,"right":0,"bottom":0})
    fit = c.get("fit","nine")
    l,t,r,b = c.get("left",0), c.get("top",0), c.get("right",0), c.get("bottom",0)
    x,y,w,h = box
    if fit == "contain":
        s = min(w/sw, h/sh); nw,nh = max(1,int(sw*s)), max(1,int(sh*s))
        canvas.alpha_composite(im.resize((nw,nh), Image.LANCZOS), (int(x+(w-nw)/2), int(y+(h-nh)/2)))
        return (x,y,w,h)
    if fit == "height":
        s = h/sh
        dl, dr = l*s, r*s
        if dl+dr > w:
            k = w/(dl+dr); dl*=k; dr*=k
        sxs=[0,l,sw-r,sw]; dxs=[x, x+dl, x+w-dr, x+w]
        for i in range(3):
            dw = dxs[i+1]-dxs[i]; sw2 = sxs[i+1]-sxs[i]
            if dw<=0 or sw2<=0: continue
            part = im.crop((int(sxs[i]),0,int(sxs[i+1]),sh)).resize((max(1,int(dw)), max(1,int(h))), Image.LANCZOS)
            canvas.alpha_composite(part, (int(dxs[i]), int(y)))
        return (x+dl, y, w-dl-dr, h)
    s = 1.0
    if l+r>0: s = min(s, w*BORDER_SHARE/(l+r))
    if t+b>0: s = min(s, h*BORDER_SHARE/(t+b))
    if s < MIN_SCALE: return None   # 도형으로 그린다
    dl,dr,dt,db = l*s, r*s, t*s, b*s
    sxs=[0,l,sw-r,sw]; sys=[0,t,sh-b,sh]
    dxs=[x,x+dl,x+w-dr,x+w]; dys=[y,y+dt,y+h-db,y+h]
    for iy in range(3):
        for ix in range(3):
            dw, dh = dxs[ix+1]-dxs[ix], dys[iy+1]-dys[iy]
            sw2, sh2 = sxs[ix+1]-sxs[ix], sys[iy+1]-sys[iy]
            if min(dw,dh,sw2,sh2) <= 0: continue
            part = im.crop((int(sxs[ix]),int(sys[iy]),int(sxs[ix+1]),int(sys[iy+1]))).resize((max(1,int(dw)),max(1,int(dh))), Image.LANCZOS)
            canvas.alpha_composite(part, (int(dxs[ix]), int(dys[iy])))
    return (x+dl, y+dt, w-dl-dr, h-dt-db)

def shape(dr, box, fill=(24,26,38,236), outline=(86,96,140,255)):
    x,y,w,h = box
    dr.rounded_rectangle([x,y,x+w,y+h], 12, fill=fill, outline=outline, width=2)

# 실제 논리 크기로 상태창과 메뉴를 흉내 낸다
W,H = 700, 620
cv = Image.new("RGBA",(W,H),(58,62,74,255))
dr = ImageDraw.Draw(cv)

# --- 상태창 270x292 ---
card = (20,20,270,292)
if draw_piece(cv,"panel",card) is None:
    shape(dr, card); dr.text((26,24),"panel -> 도형 (그림이 너무 큼)",fill=(200,210,230))
y = 60
for label,w,h in [("사냥 시작",242,42),("직접 조작",76,38)]:
    c = draw_piece(cv,"button",(32,y,w,h))
    if c is None: shape(dr,(32,y,w,h),(56,64,94,255))
    y += h+10
# 막대: 꽉 찬 길이로 그리고 60%만 보이게 자른다
track = draw_piece(cv,"bar_frame",(32,y,242,22)) or (32,y,242,22)
fill = Image.new("RGBA",(W,H),(0,0,0,0))
draw_piece(fill,"bar_fill_hp",(int(track[0]),int(track[1]),int(track[2]),int(track[3])))
clip = Image.new("L",(W,H),0); ImageDraw.Draw(clip).rectangle([track[0],track[1],track[0]+track[2]*0.6,track[1]+track[3]],fill=255)
cv.paste(fill, (0,0), Image.composite(fill.split()[3], Image.new("L",(W,H),0), clip))
y += 34
draw_piece(cv,"toggle_on",(32,y,56,26)); draw_piece(cv,"toggle_off",(100,y,56,26))

# --- 메뉴 340x532 ---
menu = (330,20,340,532)
if draw_piece(cv,"panel",menu) is None:
    shape(dr, menu); dr.text((336,24),"menu panel -> 도형",fill=(200,210,230))
my = 60
for label,w,h in [("사냥 시작",308,42),("근접",96,34),("저장하고 게임 종료",308,36)]:
    c = draw_piece(cv,"button",(346,my,w,h))
    if c is None: shape(dr,(346,my,w,h),(56,64,94,255))
    if c: dr.rectangle([c[0],c[1],c[0]+c[2],c[1]+c[3]], outline=(255,80,80,180))  # 글자 놓는 안쪽
    my += h+12

cv.convert("RGB").save("/tmp/ui_preview.png")
print("saved")
