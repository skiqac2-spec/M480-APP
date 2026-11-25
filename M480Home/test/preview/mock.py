#!/usr/bin/env python3
"""M480Home 홈 화면 목업 (home.c 의 배치 숫자 그대로, 320x294 + 위 작업 표시줄)"""
import sys
from PIL import Image, ImageDraw, ImageFont
W, H, SC, TB = 320, 294, 3, 26
FR = "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"; FB = "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"
def F(px, b=False): return ImageFont.truetype(FB if b else FR, px * SC)
fClock, fBold, fSmall, fLetter = F(34, 1), F(14, 1), F(12), F(18, 1)
BG=(12,12,14); CARD=(30,30,34); TEXT=(246,246,248); SUB=(150,150,158); DIM=(90,90,98); SEP=(40,40,44); ACC=(255,112,84); OK=(90,210,120)
PAL=[(220,70,96),(240,128,50),(70,140,240),(60,170,110),(150,90,230),(220,170,40),(40,170,200),(220,90,160),(110,130,150),(200,100,70)]
im = Image.new("RGB", (W*SC, (H+TB)*SC), BG); d = ImageDraw.Draw(im)
def R(x,y,w,h): return [x*SC,(y+TB)*SC,(x+w)*SC-1,(y+h+TB)*SC-1]
def fill(x,y,w,h,c): d.rectangle(R(x,y,w,h), fill=c)
def rr(x,y,w,h,r,c,outline=None,wd=0): d.rounded_rectangle(R(x,y,w,h), radius=r*SC, fill=c, outline=outline, width=wd*SC)
def txt(f,c,x,y,w,h,s,al=0):
    bb=d.textbbox((0,0),s,font=f); tw=bb[2]-bb[0]; th=f.size
    X=x*SC+(0 if al==0 else (w*SC-tw)//2 if al==1 else w*SC-tw); Y=(y+TB)*SC+(h*SC-th)//2-bb[1]//3
    d.text((X,Y),s,font=f,fill=c); return tw//SC
# WM 작업 표시줄 흉내
d.rectangle([0,0,W*SC,TB*SC],fill=(40,60,110)); d.text((6*SC,5*SC),"⊞  시작",font=F(12,1),fill=(255,255,255))
# 머리
x=12; cw=txt(fClock,TEXT,x,4,120,42,"08:25"); x+=cw+10
txt(fBold,TEXT,x,10,90,18,"11월 26일"); txt(fSmall,SUB,x,28,90,16,"목요일")
bx=W-12-26-3; by=12; rr(bx,by,26,13,2,None,SUB,1); fill(bx+26,by+4,3,5,SUB); fill(bx+2,by+2,22*76//100,9,TEXT)
txt(fBold,TEXT,bx-80,by-2,74,17,"76%",2)
d.ellipse(R(W-22,33,9,9),fill=OK); txt(fSmall,SUB,W-130,29,104,16,"인터넷 연결됨",2)
fill(0,49,W,1,SEP)
# 칸
apps=["웹 브라우저","유튜브","음악","만화","게임 에뮬","피처폰 게임","심연의 미궁","통화","파일 탐색기","설정"]
cols,rows=3,3; top=52; bottom=H-44-12; gw=(W-8)//cols; gh=(bottom-top)//rows
def h(s):
    v=5381
    for ch in s: v=(v*33+ord(ch))&0xffffffff
    return v
for i in range(9):
    X=4+(i%cols)*gw; Y=top+(i//cols)*gh; b=min(gh-22,gw-24,58); bx=X+(gw-b)//2; byy=Y+(gh-b-16)//2
    if i==0: rr(X+2,Y+2,gw-4,gh-4,10,None,(255,150,120),2)
    if i<len(apps):
        n=apps[i]; rr(bx,byy,b,b,b//4,PAL[h(n)%10]); txt(F(20,1),TEXT,bx,byy,b,b,n[0],1)
        txt(fBold,TEXT,X+2,byy+b+1,gw-4,16,n,1)
# 점
dy=H-44-6; dx=W//2-8
for i in range(2): d.ellipse(R(dx+i*16-(4 if i==0 else 3),dy-(4 if i==0 else 3),8 if i==0 else 6,8 if i==0 else 6),fill=TEXT if i==0 else DIM)
# 아래 단추
y=H-44; fill(0,y,W,1,SEP); bw=W//3
for i,l in enumerate(["전체 앱","실행 중","설정"]):
    xx=i*bw; cx=xx+22; cy=y+22
    if i==0:
        for a in (-6,1):
            for c in (-6,1): fill(cx+a,cy+c,5,5,TEXT)
    elif i==1: fill(cx-7,cy-6,10,8,TEXT); fill(cx-3,cy-2,10,8,BG); fill(cx-2,cy-1,8,6,TEXT)
    else:
        for k in (-1,0,1): fill(cx-7,cy+k*5,14,2,TEXT); d.ellipse(R(cx+(3 if k==0 else -3)-2,cy+k*5-2,5,5),fill=TEXT)
    txt(fBold,TEXT,xx+34,y,bw-38,44,l)
    if i==1: d.ellipse(R(xx+bw-24,y+6,16,16),fill=ACC); txt(fSmall,TEXT,xx+bw-24,y+6,16,16,"3",1)
im=im.resize((W*2,(H+TB)*2),Image.LANCZOS); im.save(sys.argv[1] if len(sys.argv)>1 else "preview.png")
