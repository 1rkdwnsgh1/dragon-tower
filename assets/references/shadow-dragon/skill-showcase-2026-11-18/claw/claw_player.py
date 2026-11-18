from pathlib import Path
import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk, ImageDraw
import time, sys, shutil

HERE=Path(__file__).resolve().parent
body=Image.open(HERE/'body.png').convert('RGBA')
effect=Image.open(HERE/'effect.png').convert('RGBA')
bc=body.height;fc=effect.height
bodies=[body.crop((i*bc,0,(i+1)*bc,bc)) for i in range(16)]
effects=[effect.crop((i*fc,0,(i+1)*fc,fc)) for i in range(8)]
durations=[130,100,100,110,110,120,140,150,75,40,90,55,80,100,130,650]
def render(i,show=True,flip=False,guides=False):
 cv=Image.new('RGBA',(1000,580),(32,30,42,255))
 b=bodies[i].resize((440,440),Image.Resampling.LANCZOS)
 cv.alpha_composite(b,(100,80))
 # Strike stays in world space after release instead of following recovery paw.
 if show and 8<=i<=14:
  f=effects[i-8].resize((330,330),Image.Resampling.LANCZOS)
  cv.alpha_composite(f,(450,180))
 if guides:
  d=ImageDraw.Draw(cv); d.line((450,180,450,510),fill=(190,140,240),width=1)
 if flip:cv=cv.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
 return cv.convert('RGB')

if '--export' in sys.argv:
 frames=[render(i) for i in range(16)]
 frames[0].save(HERE/'claw-on-dragon.gif',save_all=True,append_images=frames[1:],duration=durations,loop=0,disposal=2)
 frames[10].save(HERE/'claw-on-dragon-check.png')
 sys.exit()

root=tk.Tk();root.title('그림자드래곤 · 앞발 가르기 강화 모션 미리보기');root.configure(bg='#201e2a')
running=True;idx=0;last=time.monotonic()
bar=ttk.Frame(root);bar.pack(fill='x',padx=10,pady=10)
show=tk.BooleanVar(value=True);flip=tk.BooleanVar();guides=tk.BooleanVar();speed=tk.DoubleVar(value=1)
def draw():
 global photo
 photo=ImageTk.PhotoImage(render(idx,show.get(),flip.get(),guides.get()));canvas.configure(image=photo)
 status.configure(text=f'프레임 {idx+1}/16 · '+('타격' if idx in (9,10,11) else '준비' if idx<9 else '복귀')+'  |  본체 16프레임 · 절단 8프레임 · 고정 잔류 궤적')
def toggle():
 global running,last
 running=not running;last=time.monotonic();play.configure(text='일시정지' if running else '재생')
def step(delta):
 global idx,running
 running=False;play.configure(text='재생');idx=(idx+delta)%16;draw()
play=ttk.Button(bar,text='일시정지',command=toggle);play.pack(side='left')
ttk.Button(bar,text='◀ 이전',command=lambda:step(-1)).pack(side='left')
ttk.Button(bar,text='다음 ▶',command=lambda:step(1)).pack(side='left')
for label,var in [('효과',show),('좌우 반전',flip),('연결점',guides)]:ttk.Checkbutton(bar,text=label,variable=var,command=draw).pack(side='left',padx=6)
ttk.Label(bar,text='속도').pack(side='left');ttk.Scale(bar,from_=.25,to=1.5,variable=speed).pack(side='left')
canvas=ttk.Label(root);canvas.pack();status=ttk.Label(root);status.pack(fill='x',padx=12,pady=10)
ttk.Label(root,text='로컬 연출 확인용 · 실제 게임의 피해 판정은 변경하지 않음').pack(pady=5)
root.bind('<space>',lambda e:toggle());root.bind('<Right>',lambda e:step(1));root.bind('<Left>',lambda e:step(-1))
def tick():
 global idx,last
 now=time.monotonic()
 if running and (now-last)*1000>=durations[idx]/speed.get():idx=(idx+1)%16;last=now;draw()
 root.after(12,tick)
draw();tick();root.mainloop()
