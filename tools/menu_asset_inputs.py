"""Resolve menu textures declared by the compiled UI, including separate cooks."""
import hashlib,json,re
from pathlib import Path

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def compiled_menu_textures(root):
    seen=set();textures=set()
    def visit(path):
        path=path.resolve()
        if path in seen:return
        seen.add(path);text=path.read_text()
        textures.update(re.findall(r'"(textures/[^"\n]+\.t3x)"',text))
        for name in re.findall(r'^\s*#include\s*[<"](mkw/[^>"]+)[>"]',text,re.M):
            visit(root/'include'/name)
    visit(root/'include/mkw/menu_assets.hpp')
    return textures

def supplemental_menu_textures(root):
    sources={}
    footer=root/'build/menu-footer-hw57'
    receipt=json.loads((footer/'receipt.json').read_text())
    if receipt['fontSha256']!=digest(root/'assets/race-ui/Font/kart_kanji_font.brfnt'):
        raise ValueError('Menu footer font changed; recook footer captions')
    for row in receipt['files']:
        name=row['path'];relative=Path(name)
        if relative.parent!=Path('textures') or relative.suffix!='.t3x' or '..' in relative.parts:
            raise ValueError('Invalid menu footer texture path')
        source=footer/relative
        if digest(source)!=row['sha256']:raise ValueError('Menu footer texture changed: '+name)
        sources[name]=source
    cup=root/'build/cup-record-icons'
    receipt=json.loads((cup/'record-icon-receipt.json').read_text())
    if receipt['font_sha256']!=digest(root/'assets/race-ui/Font/tt_kart_extension_font.brfnt') or receipt['bmg_sha256']!=digest(root/'assets/menu-ui/MenuSingle_U/message/Menu.bmg'):
        raise ValueError('Cup record icon sources changed; recook the atlas')
    name=receipt['texture']
    if name!='textures/ui_cup_record_icons.t3x':raise ValueError('Invalid cup record icon texture path')
    source=cup/name
    if not source.is_file() or not source.stat().st_size:raise ValueError('Missing cup record icon atlas')
    sources[name]=source
    return sources
