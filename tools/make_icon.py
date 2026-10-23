"""FinanceWiki library cover, 520x804 grayscale (same ratio as the
Bluetooth + Key Mapping cover).
Usage: make_icon.py out.png font.otf [subtitle [mark]]   (defaults: "Kindle Edition", "FW")
The published covers use Noto Sans CJK KR Regular (OTF)."""
from PIL import Image, ImageDraw, ImageFont
import sys
W,H=520,804; K=4
img=Image.new("L",(W*K,H*K),255); d=ImageDraw.Draw(img)
s=lambda v:int(round(v*K))
FONT=sys.argv[2]
def f(size): return ImageFont.truetype(FONT,s(size))
L,R=42,W-42
# black header with framed FW mark
d.rectangle([0,0,s(W),s(250)],fill=0)
d.rectangle([s(L),s(42),s(R),s(208)],outline=255,width=s(5))
mark=sys.argv[4] if len(sys.argv)>4 else "FW"
fw=f(112 if mark=="FW" else 72); b=d.textbbox((0,0),mark,font=fw)
d.text((s(L+44)-b[0],s(125)-(b[1]+b[3])/2),mark,font=fw,fill=255)
def rule(y,w=6): d.rectangle([s(L),s(y),s(R),s(y+w)],fill=0)
def text(t,y,size):
    ft=f(size); b=d.textbbox((0,0),t,font=ft); d.text((s(L)-b[0],s(y)-b[1]),t,font=ft,fill=0)
rule(300)
text("FINANCE",350,72)
text("WIKI",455,108)
rule(600)
text("PRIVATE RESEARCH READER",632,24)
text(sys.argv[3] if len(sys.argv)>3 else "Kindle Edition",678,26)
rule(738,3)
text("financewiki",756,20)
img.resize((W,H),Image.LANCZOS).save(sys.argv[1],optimize=True)
