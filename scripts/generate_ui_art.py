"""Original vector-style artwork rasterized with supersampling for LVGL RGB565+A."""
from PIL import Image, ImageDraw
from pathlib import Path
from math import sin, cos, pi
S=4
assets=[]
def asset(name,w,h,draw):
 im=Image.new('RGBA',(w*S,h*S)); d=ImageDraw.Draw(im)
 class Pen:
  def ellipse(self,b,c):d.ellipse(tuple(int(x*S) for x in b),fill=c)
  def rect(self,b,c,r=0):d.rounded_rectangle(tuple(int(x*S) for x in b),radius=r*S,fill=c)
  def grad(self,b,top,bottom,r=0):
   box=tuple(int(x*S) for x in b); mask=Image.new('L',im.size); md=ImageDraw.Draw(mask)
   md.rounded_rectangle(box,radius=r*S,fill=255)
   layer=Image.new('RGBA',im.size); ld=ImageDraw.Draw(layer)
   a=tuple(bytes.fromhex(top.lstrip('#'))); z=tuple(bytes.fromhex(bottom.lstrip('#')))
   for y in range(box[1],box[3]+1):
    t=(y-box[1])/max(1,box[3]-box[1]); c=tuple(round(v+(w-v)*t) for v,w in zip(a,z))+(255,)
    ld.line((box[0],y,box[2],y),fill=c)
   im.paste(layer,(0,0),mask)
  def gradpoly(self,points,top,bottom):
   mask=Image.new('L',im.size);md=ImageDraw.Draw(mask)
   md.polygon([(round(x*S),round(y*S)) for x,y in points],fill=255)
   layer=Image.new('RGBA',im.size);ld=ImageDraw.Draw(layer)
   a=tuple(bytes.fromhex(top.lstrip('#')));z=tuple(bytes.fromhex(bottom.lstrip('#')))
   y0=round(min(y for x,y in points)*S);y1=round(max(y for x,y in points)*S)
   for y in range(y0,y1+1):
    t=(y-y0)/max(1,y1-y0);c=tuple(round(v+(w-v)*t) for v,w in zip(a,z))+(255,)
    ld.line((0,y,im.width,y),fill=c)
   im.paste(layer,(0,0),mask)
  def poly(self,p,c):d.polygon([(int(x*S),int(y*S)) for x,y in p],fill=c)
  def line(self,p,c,width=1):d.line([(int(x*S),int(y*S)) for x,y in p],fill=c,width=width*S)
 draw(Pen()); im=im.resize((w,h),Image.Resampling.LANCZOS);assets.append((name,im))
def plane(p):
 p.poly([(16,1),(21,15),(31,24),(30,28),(20,25),(20,30),(12,30),(12,25),(2,28),(1,24),(11,15)],'#183c68')
 p.poly([(16,2),(19,16),(29,24),(28,25),(19,22),(18,28),(14,28),(13,22),(4,25),(3,24),(13,16)],'#b9f1ff')
 p.poly([(16,7),(18,19),(16,23),(14,19)],'#24bce8');p.line([(8,22),(12,20)],'#ffffff',2)
 p.rect((14,27,18,32),'#ffbf5b',1)
def enemy(p):
 p.poly([(3,2),(10,6),(16,2),(22,6),(29,2),(26,18),(19,19),(16,27),(13,19),(6,18)],'#522d57')
 p.poly([(5,4),(11,9),(16,5),(21,9),(27,4),(24,16),(18,16),(16,23),(14,16),(8,16)],'#fa7380')
 p.rect((13,9,19,15),'#ffe7a4',2);p.line([(8,11),(10,15)],'#ffd0b5',2)
asset('fighter',32,34,plane);asset('enemy',32,28,enemy)
def cloud(p):
 p.ellipse((2,18,48,49),'#b5d5ed');p.ellipse((22,4,69,48),'#d8eaf6');p.ellipse((48,15,84,48),'#e9f5ff');p.rect((18,26,72,48),'#e9f5ff',10)
 p.ellipse((10,18,43,42),'#f5fbff');p.ellipse((26,8,63,42),'#f5fbff')
asset('cloud',88,52,cloud)
def sun(p):
 p.ellipse((0,0,80,80),(255,222,155,20));p.ellipse((8,8,72,72),(255,219,150,40));p.ellipse((16,16,64,64),'#ffd787');p.ellipse((19,18,59,57),'#ffe8ad')
asset('sun',82,82,sun)
def moon(p):
 p.ellipse((10,8, 62,60),'#dae8ff');p.ellipse((18,14,29,25),'#b5cdea');p.ellipse((40,35,54,49),'#c0d6ef');p.ellipse((25,40,32,47),'#c0d6ef')
asset('moon',72,72,moon)
def tile(k):
 def draw(p):
  if k==0: # carrot
   p.poly([(12,9),(25,14),(12,30),(7,32)],'#c77432');p.poly([(11,8),(24,13),(10,29),(8,29)],'#ffad53');p.line([(13,15),(17,18)],'#ef823b',2);p.poly([(18,11),(15,1),(20,4),(24,0),(25,6),(30,4),(25,11)],'#4c9c66')
  elif k==1: # mushroom
   p.rect((13,16,23,31),'#ddcfa9',4);p.rect((14,17,21,29),'#fff2ce',3);p.ellipse((3,3,32,23),'#af5260');p.ellipse((4,3,31,20),'#ee8790');p.ellipse((9,7,14,12),'#fff0d9');p.ellipse((22,9,27,14),'#fff0d9')
  elif k==2: # corn
   p.ellipse((9,3,26,29),'#e9ae43');
   for x in (13,19):
    for y in (7,12,17,22):p.rect((x,y,x+4,y+4),'#ffe099',1)
   p.poly([(7,12),(14,21),(18,30),(8,26)],'#65a77a');p.poly([(29,12),(25,28),(17,31),(20,22)],'#388a67')
  elif k==3: # apple
   p.ellipse((4,10,21,30),'#df6670');p.ellipse((14,9,31,30),'#ee7f7c');p.rect((12,18,24,31),'#e87778',5);p.line([(18,12),(19,4)],'#795547',3);p.ellipse((20,3,30,10),'#75ac79');p.ellipse((8,14,12,20),'#ffc1af')
  elif k==4: # flower
   for x,y in ((9,4),(20,5),(24,16),(16,23),(5,19),(3,9)):p.ellipse((x,y,x+11,y+11),'#cfaff0')
   p.ellipse((11,11,25,25),'#ffe4a2');p.ellipse((15,15,20,20),'#d3a458')
  elif k==5: # sheep
   for x,y in ((5,8),(12,4),(20,7),(23,14),(16,20),(7,20),(3,15)):p.ellipse((x,y,x+11,y+11),'#e8e6d6')
   p.ellipse((10,10,27,27),'#fffdf0');p.ellipse((16,13,26,26),'#776f75');p.ellipse((21,16,23,18),'#fffdf0');p.line([(10,27),(10,32)],'#776f75',3);p.line([(23,27),(23,32)],'#776f75',3)
  elif k==6: # watering can
   p.poly([(7,15),(0,8),(2,5),(13,14)],'#66a9c5');p.rect((10,12,28,30),'#76c0d1',4);p.rect((23,10,34,22),'#5ba2b9',4);p.rect((26,13,31,19),'#d9efdf',2);p.rect((10,12,27,16),'#a5e1e4',3);p.line([(15,19),(15,25)],'#bcebed',2)
  elif k==7: # wheat
   p.line([(17,4),(17,32)],'#be914f',2)
   for y in (5,12,19):p.ellipse((7,y,16,y+10),'#e5bc69');p.ellipse((18,y,27,y+10),'#f4d78a')
  else: # leaf
   p.ellipse((6,3,29,29),'#66a979');p.poly([(6,26),(17,10),(27,4),(27,20),(12,29)],'#8ec899');p.line([(9,29),(24,10)],'#d6edba',2)
 return draw
for i in range(9):asset('tile_'+str(i),36,36,tile(i))
# App artwork shares the USB icon's soft gradients, cool highlights and generous silhouette.
def app_icon(kind):
 def draw(p):
  if kind=='voice':
   p.grad((23,3,47,40),'#a4f5e5','#2f9fb2',12)
   for y in (12,18,24): p.rect((30,y,40,y+2),'#eaffff',1)
   p.line([(16,27),(16,37),(22,46),(35,49),(48,46),(54,37),(54,27)],'#b9e9ed',3)
   p.rect((33,48,37,55),'#b9e9ed',2);p.rect((24,54,46,58),'#76e4d4',2)
  elif kind=='music':
   p.grad((12,7,58,54),'#f5a5c6','#ba4a83',13)
   p.rect((16,13,19,38),'#ffd3e4',2)
   p.line([(32,41),(32,21),(48,17),(48,37)],'#fff4fa',4)
   p.line([(33,26),(47,22)],'#fff4fa',3)
   p.ellipse((23,36,34,45),'#fff4fa');p.ellipse((39,32,50,41),'#fff4fa')
  elif kind=='games':
   p.grad((5,16,65,50),'#cab6ff','#8063c8',14)
   p.rect((11,21,28,24),'#e5d9ff',2)
   p.rect((16,28,22,43),'#f3edff',2);p.rect((11,33,27,38),'#f3edff',2)
   p.ellipse((47,26,54,33),'#ffe2ad');p.ellipse((54,34,61,41),'#b6f1e7')
   p.rect((31,39,39,42),'#5b478f',2)
  elif kind=='pictures':
   p.grad((9,7,61,54),'#ffe0a3','#d49a4e',9)
   p.rect((14,12,56,49),'#254f68',5);p.ellipse((40,17,50,27),'#ffdf92')
   p.poly([(15,43),(29,25),(43,44)],'#93dbcd');p.poly([(30,48),(45,31),(56,45),(56,49)],'#52adaf')
   p.rect((15,12,52,14),'#fff0c9',1)
  elif kind=='settings':
   # Eight machined teeth, a bevelled rim and a recessed circular centre.
   gear=[]
   for i in range(8):
    for angle,r in ((-22.5,21),(-14,21),(-10,27),(10,27),(14,21)):
     t=(i*45+angle)*pi/180;gear.append((35+cos(t)*r,30+sin(t)*r))
   p.gradpoly([(x,y+2) for x,y in gear],'#748ca8','#435e80')
   p.gradpoly(gear,'#e0edf7','#829fbf')
   p.ellipse((16,11,54,49),'#d3e4f2')
   p.ellipse((18,13,52,47),'#90aec9')
   p.ellipse((21,16,49,44),'#52708d')
   p.ellipse((24,19,46,41),'#20394e')
   p.line([(27,18),(34,16),(40,17)],'#edf7ff',2)
  elif kind=='files':
   p.rect((7,10,33,27),'#b6deff',6);p.rect((10,16,61,49),'#779dcc',6)
   p.rect((16,20,54,45),'#e0f0ff',3)
   p.grad((7,26,64,55),'#87c8f7','#427caf',7);p.rect((12,30,57,33),'#c3eaff',2)
  elif kind=='home':
   # One cohesive house silhouette; white connected nodes stay legible at 70px.
   p.rect((47,9,54,24),'#a5e5f0',2)
   house=[(8,27),(32,6),(35,5),(38,6),(62,27),(61,31),(56,31),(56,51),(52,56),(18,56),(14,51),(14,31),(9,31)]
   p.gradpoly([(x,y+2) for x,y in house],'#399cb4','#28758f')
   p.gradpoly(house,'#9de9eb','#369fb7')
   p.line([(13,27),(34,9),(36,9)],'#dbffff',2)
   p.rect((18,34,20,48),'#9fe7e7',1)
   p.line([(35,48),(35,31),(27,25)],'#f1ffff',3)
   p.line([(35,41),(45,34),(45,27)],'#f1ffff',3)
   p.ellipse((23,21,31,29),'#f1ffff')
   p.ellipse((41,23,49,31),'#f1ffff')
   p.ellipse((31,44,39,52),'#f1ffff')
  elif kind=='shooter':
   p.rect((11,18,14,30),'#709bb8',2);p.rect((56,9,59,24),'#709bb8',2)
   p.grad((30,40,40,59),'#ffdf99','#ef9561',5)
   p.poly([(35,2),(43,25),(62,40),(61,46),(42,40),(41,51),(29,51),(28,40),(9,46),(8,40),(27,25)],'#bcefff')
   p.poly([(35,7),(40,29),(35,38),(30,29)],'#409ebf')
   p.line([(16,40),(27,34)],'#ffffff',2);p.line([(43,34),(54,40)],'#ffffff',2)
  elif kind=='tiles':
   p.rect((8,6,48,46),'#6cad99',7);p.rect((15,11,55,51),'#b8dec4',7)
   p.grad((22,17,63,58),'#fff8df','#dfd8b5',7)
   for x,y in ((29,28),(37,24),(45,28),(29,37),(45,37)):p.ellipse((x,y,x+12,y+12),'#ffffff')
   p.ellipse((37,32,49,48),'#6b7983');p.ellipse((39,36,41,38),'#ffffff');p.ellipse((45,36,47,38),'#ffffff')
  elif kind=='flood':
   colors=[('#b9a3f4','#8967c5'),('#9ce7db','#4ba99f'),('#ffda9b','#d9a258'),('#9ce7db','#4ba99f'),('#ffda9b','#d9a258'),('#f6a5b8','#c66c91'),('#ffda9b','#d9a258'),('#f6a5b8','#c66c91'),('#a4d4ff','#6599d0')]
   for i,(a,b) in enumerate(colors):
    x=9+(i%3)*18;y=4+(i//3)*18;p.grad((x,y,x+15,y+15),a,b,4);p.rect((x+3,y+2,x+11,y+3),'#edf7ff',1)
 return draw
for name in ('voice','music','games','pictures','settings','files','home','shooter','tiles','flood'):
 asset('icon_'+name,70,60,app_icon(name))
 if name in ('shooter','tiles','flood'):
  assets.append(('icon_'+name+'_small',assets[-1][1].resize((47,40),Image.Resampling.LANCZOS)))

out=['// Generated by scripts/generate_ui_art.py; original artwork.','#include "ui_art.h"']
head=['#pragma once','#include "lvgl.h"']
for name,im in assets:
 data=[]
 for r,g,b,a in im.getdata():
  v=((r>>3)<<11)|((g>>2)<<5)|(b>>3);data.extend((v>>8,v&255,a))
 out.append('static const uint8_t data_'+name+'[] = {\n'+',\n'.join(','.join(str(v) for v in data[i:i+36]) for i in range(0,len(data),36))+'\n};')
 out.append(f'const lv_img_dsc_t art_{name} = {{.header={{.cf=LV_IMG_CF_TRUE_COLOR_ALPHA,.w={im.width},.h={im.height}}},.data_size=sizeof(data_{name}),.data=data_{name}}};')
 head.append(f'extern const lv_img_dsc_t art_{name};')
Path('src/assets/ui_art.c').write_text('\n'.join(out)+'\n');Path('src/assets/ui_art.h').write_text('\n'.join(head)+'\n')
preview=Image.new('RGB',(440,120),'#d9efdf')
for i,(name,im) in enumerate(assets):
 if name.startswith('tile'):preview.paste(im,(8+(i-5)*46,40),im)
Path('artifacts').mkdir(exist_ok=True);preview.save('artifacts/game-art.png')

icons=[(n,im) for n,im in assets if n.startswith('icon_') and not n.endswith('_small')]
preview=Image.new('RGB',(5*100,2*88),'#1b2b40')
for i,(name,im) in enumerate(icons):preview.paste(im,((i%5)*100+15,(i//5)*88+12),im)
preview.save('artifacts/app-icons.png')
