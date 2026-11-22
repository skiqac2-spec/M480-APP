#!/usr/bin/env python3
"""M480Music 화면 미리보기 (player.c 의 그리기 코드를 PIL 로 옮긴 목업, 320x294)"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H, SC = 320, 294, 4          # 4배로 그린 뒤 줄여서 미리보기
def S(x): return x
FONT = "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"
FONTB = "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"
def F(px, b=False): return ImageFont.truetype(FONTB if b else FONT, px * SC)
fTitle, fHead, fBold, fNorm, fSmall = F(24, 1), F(17, 1), F(14, 1), F(14), F(12)
fGlyphS, fGlyphB = F(20, 1), F(56, 1)

C_BG=(12,12,14); C_CARD=(30,30,34); C_PRESS=(46,46,52); C_TEXT=(246,246,248); C_SUB=(150,150,158)
C_DIM=(105,105,112); C_SEP=(38,38,42); C_ACCENT=(255,112,84); C_MINI=(28,28,32)
PAL=[((236,72,98),(120,24,60)),((255,140,60),(150,50,20)),((90,160,255),(30,50,140)),((120,210,150),(20,90,70)),
     ((180,110,255),(70,30,140)),((255,200,70),(160,90,10)),((70,200,220),(20,80,110)),((240,110,180),(110,30,90))]

def mix(a,b,t): return tuple((a[i]*(256-t)+b[i]*t)>>8 for i in range(3))
def hsh(s):
    h=5381
    for ch in s: h=(h*33+ord(ch))&0xffffffff
    return h

class G:
    def __init__(s): s.im=Image.new("RGB",(W*SC,H*SC)); s.d=ImageDraw.Draw(s.im)
    def fill(s,x,y,w,h,c): s.d.rectangle([x*SC,y*SC,(x+w)*SC-1,(y+h)*SC-1],fill=c)
    def rrect(s,x,y,w,h,r,c): s.d.rounded_rectangle([x*SC,y*SC,(x+w)*SC,(y+h)*SC],radius=r*SC,fill=c)
    def circle(s,cx,cy,r,c): s.d.ellipse([(cx-r)*SC,(cy-r)*SC,(cx+r)*SC,(cy+r)*SC],fill=c)
    def poly(s,p,c): s.d.polygon([(x*SC,y*SC) for x,y in p],fill=c)
    def lines(s,p,w,c): s.d.line([(x*SC,y*SC) for x,y in p],fill=c,width=w*SC,joint="curve")
    def tw(s,t,f): return s.d.textlength(t,font=f)/SC
    def text(s,t,x,y,w,h,f,c,al="L"):
        while t and s.tw(t,f)>w: t=t[:-2]+"…" if len(t)>2 else ""
        tw=s.tw(t,f); asc,desc=f.getmetrics(); th=(asc+desc)/SC
        tx = x if al=="L" else (x+(w-tw)/2 if al=="C" else x+w-tw)
        s.d.text((tx*SC,(y+(h-th)/2)*SC),t,font=f,fill=c)
    def grad(s,x,y,w,h,a,b):
        steps=max(1,h//4)
        for i in range(steps):
            y0=y+h*i//steps; y1=y+h*(i+1)//steps; s.fill(x,y0,w,y1-y0,mix(a,b,i*256//steps))

def icon_play(g,cx,cy,sz,c): g.poly([(cx-sz*4/10,cy-sz/2),(cx-sz*4/10,cy+sz/2),(cx+sz/2,cy)],c)
def icon_pause(g,cx,cy,sz,c):
    bw=sz*3//10+1; g.rrect(cx-sz/2+1,cy-sz/2,bw,sz,1,c); g.rrect(cx+sz/2-bw,cy-sz/2,bw,sz,1,c)
def icon_skip(g,cx,cy,sz,d,c):
    h=w=sz/2
    for k in range(2):
        x0=cx+(0 if k else -w)*d; g.poly([(x0,cy-h),(x0,cy+h),(x0+w*d,cy)],c)
def icon_dots(g,cx,cy,c):
    for dx in(-6,0,6): g.circle(cx+dx,cy,2,c)
def icon_shuffle(g,cx,cy,c):
    w,h=7,5
    g.lines([(cx-w,cy-h),(cx+w-2,cy+h)],2,c); g.lines([(cx-w,cy+h),(cx+w-2,cy-h)],2,c)
    g.poly([(cx+w-4,cy+h-3),(cx+w+1,cy+h),(cx+w-4,cy+h+3)],c); g.poly([(cx+w-4,cy-h-3),(cx+w+1,cy-h),(cx+w-4,cy-h+3)],c)

def art(g,title,sub,x,y,s,big,img=None):
    if img is not None:
        g.im.paste(img.resize((s*SC,s*SC)),(x*SC,y*SC))
    else:
        p=PAL[hsh(sub or title)&7]; g.grad(x,y,s,s,p[0],p[1])
        g.text("♪",x,y,s,s,fGlyphB if big else fGlyphS,(255,255,255),"C")

SONGS=[("달빛 밴드","밤 산책"),("Coldplay","Viva La Vida"),("Daft Punk","Get Lucky"),("Gerry Rafferty","Right Down the Line"),
       ("Music","Get Back (The Rooftop Concert 1969)"),("Godfather 2","Chris Whiteley - Moving"),("Norah Jones","Don't Know Why"),
       ("Queen","Bohemian Rhapsody"),("김광석","서른 즈음에"),("새소년","난춘")]
COVER=Image.open(sys.argv[2]) if len(sys.argv)>2 else None
CUR=0

def lib(g,playing=True,press=-1,scroll=0):
    rowH,miniH,top=40,50,46; bot=H-miniH; art_=rowH-10
    g.fill(0,0,W,H,C_BG); hy=-scroll
    if hy+top>0:
        pw,ph=62,28; py=hy+9; px2=W-44-pw; px1=px2-6-pw
        g.text("보관함",14,hy+4,px1-14,38,fTitle,C_TEXT)
        g.rrect(px1,py,pw,ph,14,C_CARD); g.rrect(px2,py,pw,ph,14,C_CARD)
        t=g.tw("재생",fBold); icon_play(g,px1+pw/2-t/2-6,py+ph/2,10,C_ACCENT); g.text("재생",px1+pw/2-t/2+3,py,t+4,ph,fBold,C_ACCENT)
        t=g.tw("셔플",fBold); icon_shuffle(g,px2+pw/2-t/2-8,py+ph/2,C_ACCENT); g.text("셔플",px2+pw/2-t/2+5,py,t+4,ph,fBold,C_ACCENT)
        icon_dots(g,W-22,py+ph/2,C_ACCENT)
    for i,(a,t) in enumerate(SONGS):
        ry=top+i*rowH-scroll
        if ry>=bot: break
        if ry+rowH<=0: continue
        bg=C_PRESS if i==press else C_BG
        g.fill(0,ry,W,rowH,bg)
        art(g,t,a,12,ry+5,art_,0,COVER if i==CUR else None)
        if i==CUR:
            for k,hh in enumerate((6,9,7)):
                s_=art_; bw=s_//8; gap=s_//12; x0=12+(s_-(bw*3+gap*2))//2; base=ry+5+s_*3//4; hh=s_*hh//20
                g.fill(x0+k*(bw+gap),base-hh,bw,hh,(255,255,255))
        g.text(t,12+art_+10,ry+3,W-art_-34,19,fBold,C_ACCENT if i==CUR else C_TEXT)
        g.text(a,12+art_+10,ry+20,W-art_-34,16,fSmall,C_SUB)
        g.fill(12+art_+10,ry+rowH-1,W,1,C_SEP)
    g.rrect(W-4,(bot-60)*scroll//200,3,60,1,(70,70,78))
    my=H-miniH; a=miniH-14
    g.fill(0,my,W,miniH,C_MINI); g.fill(0,my,W,2,(55,55,60)); g.fill(0,my,W*35//100,2,C_ACCENT)
    art(g,"밤 산책","달빛 밴드",10,my+8,a,0,COVER)
    g.text("밤 산책",10+a+10,my+6,W-a-110,20,fBold,C_TEXT); g.text("달빛 밴드",10+a+10,my+25,W-a-110,16,fSmall,C_SUB)
    (icon_pause if playing else icon_play)(g,W-66,my+miniH/2,18,C_TEXT); icon_skip(g,W-24,my+miniH/2,16,1,C_TEXT)

def lib2(g): lib(g,scroll=120,press=6)

def now(g,playing=True):
    tint=(150,110,90) if COVER else PAL[3][1]
    top=mix(C_BG,tint,150); bot=mix(C_BG,tint,30)
    g.grad(0,0,W,H,top,bot)
    g.lines([(22-8,15-3),(22,15+4),(22+8,15-3)],3,(220,220,228))
    g.text("지금 재생 중",0,0,W,30,fSmall,(210,210,218),"C"); icon_dots(g,W-22,15,(220,220,228))
    fixedSide=30+8+14+30+56+36+6; A=min(H-fixedSide,W//2); ax,ay=16,38
    art(g,"밤 산책","달빛 밴드",ax,ay,A,1,COVER)
    tx=ax+A+12; tw=W-tx-14
    g.text("밤 산책",tx,ay+2,tw,22,fHead,C_TEXT); g.text("달빛 밴드",tx,ay+2+26,tw,18,fNorm,(225,225,232))
    g.text("여름밤",tx,ay+2+44,tw,16,fSmall,(170,170,180)); g.text("MP3 · 192kbps",tx,ay+A-16,tw,16,fSmall,(150,150,160))
    by=ay+A+14; bl,br=18,W-18; onb=mix(top,bot,by*256//H)
    g.rrect(bl,by,br-bl,4,2,mix(onb,(255,255,255),70)); fw=(br-bl)*35//100
    g.rrect(bl,by,fw,4,2,(240,240,245)); g.circle(bl+fw,by+2,5,(255,255,255))
    g.text("1:10",bl,by+8,60,16,fSmall,(190,190,198)); g.text("3:20",br-60,by+8,60,16,fSmall,(190,190,198),"R")
    cy=by+30+26; cx=W//2
    g.circle(cx,cy,26,(255,255,255))
    (icon_pause(g,cx,cy,20,(20,20,24)) if playing else icon_play(g,cx+2,cy,22,(20,20,24)))
    icon_skip(g,cx-78,cy,22,-1,C_TEXT); icon_skip(g,cx+78,cy,22,1,C_TEXT)
    chy=cy+30; gap=6; cw=(W-32-gap*3)//4; cb=mix(mix(top,bot,chy*256//H),(255,255,255),40)
    for k,t in enumerate(["차례대로","자동 22k","이어폰","소리 보통"]):
        g.rrect(16+k*(cw+gap),chy,cw,28,14,cb); g.text(t,16+k*(cw+gap)+2,chy,cw-4,28,fSmall,(235,235,240),"C")

def settings(g):
    RH,TOP=40,48; g.fill(0,0,W,H,C_BG)
    g.rrect(10,TOP,W-20,4*RH,10,C_CARD); g.rrect(10,TOP+4*RH+14,W-20,2*RH,10,C_CARD); g.rrect(10,TOP+6*RH+28,W-20,RH,10,C_CARD)
    rows=[("음질","자동 (보통)",0),("출력 모드","이어폰",0),("소리 크기","보통",0),("재생 순서","차례대로",0),
          ("음악 목록 새로고침","15곡",0),("출력","22.0kHz 스테레오 · 끊김 0",1),("앱 종료","",2)]
    for i,(l,v,k) in enumerate(rows):
        ry=TOP+i*RH+(14 if i>=4 else 0)+(14 if i>=6 else 0)
        if k==2: g.text(l,10,ry,W-20,RH,fNorm,C_ACCENT,"C"); continue
        g.text(l,24,ry,W/2,RH,fNorm,C_TEXT)
        if k==0:
            g.text(v,W/2-20,ry,W/2-20,RH,fNorm,C_SUB,"R")
            g.lines([(W-26-3,ry+RH/2-5),(W-26+2,ry+RH/2),(W-26-3,ry+RH/2+5)],2,C_DIM)
        else: g.text(v,70,ry,W-94,RH,fSmall,C_SUB,"R")
        if i not in(3,5,6): g.fill(24,ry+RH-1,W-34,1,C_SEP)
    g.fill(0,0,W,42,C_BG)
    g.lines([(20+4,21-8),(20-4,21),(20+4,21+8)],3,C_ACCENT); g.text("설정",0,0,W,42,fHead,C_TEXT,"C")

out=sys.argv[1]
shots=[]
for fn in (lib,lib2,now,settings):
    g=G(); fn(g); shots.append(g.im.resize((W,H),Image.LANCZOS))
# 실제 크기 3장 + 2배 확대본
sheet=Image.new("RGB",(W*2*2+30,H*2*2+30),(255,255,255))
for i,im in enumerate(shots): sheet.paste(im.resize((W*2,H*2),Image.NEAREST),(10+(i%2)*(W*2+10),10+(i//2)*(H*2+10)))
sheet.save(out)
