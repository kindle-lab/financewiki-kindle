"""Fixed-weight reading fonts from the 0.7.0 variable fonts.

The 0.7.0 reader loaded Noto Serif/Sans CJK KR VF directly; FBInk (stb_truetype)
ignores font variations, so it drew their default instances (Serif 200, Sans 100),
which are too faint on e-ink. This pins heavier weights:

    python3 tools/make_fonts.py
    -> fonts/NotoSerifCJKkr-SemiBold.ttf (wght 600, body 명조)
    -> fonts/NotoSansCJKkr-Medium.ttf    (wght 500, body 고딕 and UI)

Requires fontTools. Sources: fonts/source/*-VF.ttf (SIL OFL, see licenses/).
"""
import os
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for source, weight, target in [("NotoSerifCJKkr-VF.ttf", 600, "NotoSerifCJKkr-SemiBold.ttf"),
                               ("NotoSansCJKkr-VF.ttf", 500, "NotoSansCJKkr-Medium.ttf")]:
    font = TTFont(os.path.join(ROOT, "fonts", "source", source))
    fixed = instancer.instantiateVariableFont(font, {"wght": weight}, updateFontNames=False)
    fixed["OS/2"].usWeightClass = weight
    fixed.save(os.path.join(ROOT, "fonts", target))
    print(target, weight)
