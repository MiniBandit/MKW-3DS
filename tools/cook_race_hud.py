"""Recover the small single-player race HUD from locally extracted Nintendo layouts.

BRLYT, BRLAN and BRFNT offsets follow the bundled NW4R parser. No substituted
system fonts or hand drawn countdown glyphs are used.
"""
import io
import math
import struct
from pathlib import Path

from PIL import Image
from gclib.gx_enums import ImageFormat, PaletteFormat
from gclib.texture_utils import decode_image
from cook_effects import entries, effect


def sections(data):
    offset = struct.unpack_from(">H", data, 12)[0]
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset + 4)[0]
        if size < 8 or offset + size > len(data):
            raise ValueError("Invalid NW4R section")
        yield data[offset:offset+4], offset, offset+8
        offset += size


def string(data, offset):
    return data[offset:].split(b"\0", 1)[0].decode("ascii")


def layout(path):
    data = path.read_bytes()
    panes = {}
    for tag, _, start in sections(data):
        if tag not in (b"pan1", b"pic1", b"txt1"):
            continue
        name = string(data, start+4)
        values = struct.unpack_from(">10f", data, start+28)
        pane = dict(zip(("x", "y", "z", "rx", "ry", "rz", "sx", "sy", "w", "h"), values))
        pane["alpha"] = data[start+2]
        if tag == b"txt1":
            pane["top"], pane["bottom"] = struct.unpack_from(">2I", data, start+84)
        panes[name] = pane
    return panes


def variant(path, name):
    data = path.read_bytes()
    table, strings = struct.unpack_from(">2H", data, 14)
    count = struct.unpack_from(">H", data, table+2)[0]
    for i in range(count):
        start = table+12+i*60
        if string(data, strings+struct.unpack_from(">H", data, start)[0]) == name:
            return struct.unpack_from(">5f", data, start+32)  # 16:9 artist transform
    raise ValueError("Missing original HUD variant " + name)


def animation(path):
    data = path.read_bytes()
    tracks = {}
    for tag, block, start in sections(data):
        if tag != b"pai1":
            continue
        duration, _, _, _, count, table = struct.unpack_from(">HBBHHI", data, start)
        for i in range(count):
            bind = block+struct.unpack_from(">I", data, block+table+4*i)[0]
            name = string(data, bind)
            for j in range(data[bind+20]):
                anim = bind+struct.unpack_from(">I", data, bind+24+4*j)[0]
                kind = data[anim:anim+4].decode()
                for k in range(data[anim+4]):
                    track = anim+struct.unpack_from(">I", data, anim+8+4*k)[0]
                    target, curve = data[track+1], data[track+2]
                    count = struct.unpack_from(">H", data, track+4)[0]
                    frame = track+struct.unpack_from(">I", data, track+8)[0]
                    fmt, stride = (">3f", 12) if curve == 2 else (">fHH", 8)
                    tracks[(name, kind, target)] = [struct.unpack_from(fmt, data, frame+n*stride) for n in range(count)]
    return duration, tracks


def sample(keys, frame):
    if frame < keys[0][0]:
        return keys[0][1]
    for i in range(1, len(keys)):
        if frame < keys[i][0]:
            f0, v0, t0 = keys[i-1]
            f1, v1, t1 = keys[i]
            span = f1-f0
            t = (frame-f0)/span
            return ((2*t**3-3*t**2+1)*v0 + (t**3-2*t**2+t)*t0*span
                    + (-2*t**3+3*t**2)*v1 + (t**3-t**2)*t1*span)
    return keys[-1][1]


def font_strings(path, labels, *, metrics=None, full_ink=False):
    data = path.read_bytes()
    glyphs, widths, mapping = [], {}, {}
    for tag, _, start in sections(data):
        if tag == b"TGLP":
            cw, ch, _, _, size, count, fmt, nx, ny, w, h, pixels = struct.unpack_from(">4BI6HI", data, start)
            for i in range(count):
                sheet = decode_image(io.BytesIO(data[pixels+i*size:pixels+(i+1)*size]),
                                     io.BytesIO(), ImageFormat(fmt), PaletteFormat(0), 0, w, h).convert("RGBA")
                for y in range(ny):
                    for x in range(nx):
                        glyphs.append(sheet.crop((x*(cw+1), y*(ch+1), x*(cw+1)+cw, y*(ch+1)+ch)))
        elif tag == b"CWDH":
            first, last = struct.unpack_from(">2H", data, start)
            for i in range(first, last+1):
                widths[i] = struct.unpack_from(">bBb", data, start+8+(i-first)*3)
        elif tag == b"CMAP":
            first, last, kind = struct.unpack_from(">3H", data, start)
            if kind == 0:
                begin = struct.unpack_from(">H", data, start+12)[0]
                mapping.update({i: begin+i-first for i in range(first, last+1)})
            elif kind == 1:
                mapping.update({i: struct.unpack_from(">H", data, start+12+2*(i-first))[0] for i in range(first, last+1)})
            else:
                count = struct.unpack_from(">H", data, start+12)[0]
                mapping.update(struct.unpack_from(">2H", data, start+14+4*i) for i in range(count))
    result = {}
    for label in labels:
        indices = [mapping[ord(c)] for c in label]
        # Blend separate glyphs like CharWriter; transparent pixels must not
        # erase the preceding outline. Keep the original advances, with only
        # one source pixel of tracking (two for Pr), per the compact spacing
        # requested after reviewing the expanded ink-bound candidate.
        placements=[];x=0;leftmost=0;rightmost=0
        for at,i in enumerate(indices):
            left,width,advance=widths[i]
            # CWDH width controls layout, but some retail cells carry outline
            # ink past that width. Description captions retain the full cell;
            # advances/bearings still come from the original CWDH record.
            glyph=glyphs[i].crop((0,0,cw if full_ink else width,ch));box=glyph.getchannel("A").getbbox()
            if box:leftmost=min(leftmost,x+left+box[0]);rightmost=max(rightmost,x+left+box[2])
            placements.append((glyph,x+left));x+=advance
            if at+1<len(indices):x+=2 if label[at:at+2]=="Pr" else 1
        pad=-leftmost
        image=Image.new("RGBA",(max(1,x,rightmost)+pad,ch))
        for glyph,at in placements:
            if glyph.width:image.alpha_composite(glyph,(at+pad,0))
        if metrics is not None: metrics[label] = dict(advance=x,origin=leftmost,height=ch)
        result[label] = image
    return result


def cook_hud(root, output, manifest, sprite, image_sprite, digest):
    ui = root / "assets/race-ui/Race/game_image"
    localized = root / "assets/race-ui/Race_U/game_image"
    extras = manifest["hudExtras"] = {}
    sources = [("frame", ui/"timg/tt_item_box_glass_type_02.tpl"),
               ("lap", localized/"timg/tt_lap_E.tpl"),
               ("slash", localized/"timg/tt_d_number_3d_slash.tpl"),
               ("mushroom_two", ui/"timg/tt_item_kinoko_2.tpl")]
    for name, path in sources:
        extras[name] = sprite(path, "hud_"+name, output)
    font = root/"assets/race-ui/Font/tt_kart_font_rodan_ntlg_pro_b.brfnt"
    labels = {"count_3":"3", "count_2":"2", "count_1":"1", "go":"GO!", "finish":"FINISH!"}
    messages = (root/"assets/race-ui/Race_U/message/Race.bmg").read_bytes()
    glyphs = font_strings(font, labels.values())
    for name, label in labels.items():
        if label.encode("utf-16be")+b"\0\0" not in messages:
            raise ValueError("Countdown text missing from source BMG: " + label)
        record = image_sprite(glyphs[label], "hud_"+name, output)
        record.update(source=str(font.relative_to(root)), source_sha256=digest(font), text=label)
        extras[name] = record

    # The Blooper overlay lives in REFT/REFF, not the UI archive. Preserve its
    # original alpha mask and every baked B splat scale/fade sample.
    reft_path = root/"assets/common/Effect/RKRace.breft"
    reff_path = reft_path.with_suffix(".breff")
    reft, reff = reft_path.read_bytes(), reff_path.read_bytes()
    textures, definitions = entries(reft), entries(reff)
    ink_effect = effect(reff, definitions, "rk_gessoB_1P")
    offset, _ = textures["gessoSumi2I4"]
    w,h,size = struct.unpack_from(">HHI",reft,offset+4)
    fmt,pf,count,psize = struct.unpack_from(">BBHI",reft,offset+12)
    ink = decode_image(io.BytesIO(reft[offset+32:offset+32+size]),
                       io.BytesIO(reft[offset+32+size:offset+32+size+psize]),
                       ImageFormat(fmt),PaletteFormat(pf),count,w,h).convert("RGBA")
    record = image_sprite(ink,"hud_ink",output)
    record.update(source=str(reft_path.relative_to(root)), source_sha256=digest(reft_path), sourceTexture="gessoSumi2I4")
    extras["ink"] = record
    for name, file in [('chase_base','tt_chase_icon.tpl'),('chase_light','tt_chase_icon_light.tpl')]:
        extras[name] = sprite(ui/'timg'/file,'hud_'+name,output)
    curves = [bytes.fromhex(c) for c in ink_effect["curves"]]
    ink_scale = next(c for c in curves if c[:4] == bytes.fromhex("ab180303"))
    ink_alpha = next(c for c in curves if c[:4] == bytes.fromhex("ab030001"))
    ink_frames = [list(struct.unpack_from(">2f",ink_scale,32+8*i))+[ink_alpha[32+i]/255] for i in range(450)]

    # Keep the source's uniform shape scale and corner offsets. Its 832x456
    # widescreen canvas is adapted to 400x240, whose aspect is 5:3, by anchoring
    # left/right HUD groups independently (never stretching the glyphs).
    scale = 240/456
    def group(control, variant_name, side):
        x, y, _, sx, sy = variant(ui/"ctrl"/(control+".brctr"), variant_name)
        return ((x+416)*scale if side == "left" else 400-(416-x)*scale,
                (228-y)*scale, sx*scale, sy*scale)
    item = group("item_window_new", "CtrlRaceItemWindow_1_0", "left")
    lap = group("lap_number", "CtrlRaceLap_1_0", "right")
    rank = group("position", "CtrlRaceRankNum_1_0", "left")
    lp = layout(localized/"blyt/game_image_lap.brlyt")
    rp = layout(localized/"blyt/game_image_position.brlyt")
    rects = {}
    def rect(name, group_pose, pane, parent=None):
        gx, gy, sx, sy = group_pose
        px, py = (parent["x"], parent["y"]) if parent else (0, 0)
        w, h = pane["w"]*pane["sx"]*sx, pane["h"]*pane["sy"]*sy
        rects[name] = [gx+(px+pane["x"])*sx-w/2, gy-(py+pane["y"])*sy-h/2, w, h]
    ip = layout(ui/"blyt/item_window.brlyt")
    rect("ItemFrame", item, ip["hilight_next"])
    rect("ItemIcon", item, ip["item_next"])
    rect("LapCurrent", lap, lp["race_null"])
    rect("LapLabel", lap, lp["lap_text"], lp["race_null"])
    rect("LapSlash", lap, lp["slash"], lp["race_null"])
    rect("LapTotal", lap, lp["lap_riight"], lp["race_null"])
    rect("Rank", rank, rp["position"], rp["race_null"])
    rect("RankShadow", rank, rp["position_sha"], rp["race_null"])

    panes = layout(ui/"blyt/go.brlyt")
    duration, tracks = animation(ui/"anim/go_fade_in.brlan")
    frames = []
    def packed(channels):
        return sum(max(0,min(255,round(c))) << (24-8*i) for i,c in enumerate(channels))
    for frame in range(duration):
        layers = []
        for name, c0, c1 in [("text_00",[250,250,250,0],[255,255,255,255]),
                             ("text_01",[70,70,70,0],[255,137,137,0]),
                             ("text_light",[255,255,255,0],[255,255,255,255])]:
            pane = panes[name]
            read = lambda kind, target, default: sample(tracks[(name,kind,target)],frame) if (name,kind,target) in tracks else default
            c0 = [read("RLMC",4+i,c) for i,c in enumerate(c0)]
            c1 = [read("RLMC",8+i,c) for i,c in enumerate(c1)]
            layers.append([pane["x"]*scale, -pane["y"]*scale, read("RLPA",6,pane["sx"])*scale,
                           read("RLVC",16,pane["alpha"])/255, pane["top"],pane["bottom"],packed(c0),packed(c1)])
        frames.append(layers)
    lines = ["#pragma once", "#include <array>", "#include <cstdint>", "namespace mkw {",
             "// Generated from original BRCTR/BRLYT/BRLAN; see race-items.json hudLayout.",
             "struct HudRect {float x,y,w,h;};"]
    for name, values in rects.items():
        lines.append("inline constexpr HudRect Hud%s{%s};" % (name, ",".join(f"{v:.8f}f" for v in values)))
    lines += ["struct HudCountLayer {float x,y,scale,alpha;uint32_t top,bottom,color0,color1;};",
              f"inline constexpr std::array<std::array<HudCountLayer,3>,{duration}> HudCountFrames{{{{"]
    for layers in frames:
        rows = ["{"+",".join([*(f"{v:.8f}f" for v in layer[:4]),*(f"0x{v:08x}u" for v in layer[4:])])+"}" for layer in layers]
        lines.append("    {{"+",".join(rows)+"}},")
    lines += ["}};", "struct HudInkFrame {float scaleX,scaleY,alpha;};",
              "inline constexpr std::array<HudInkFrame,450> HudInkFrames{{"]
    lines += ["    {"+",".join(f"{v:.8f}f" for v in frame)+"}," for frame in ink_frames]
    lines += ["}};",f"inline constexpr float HudInkHalfExtent={ink_effect['size'][0]:.8f}f;",
              f"inline constexpr float HudInkDepth={-ink_effect['translate'][2]:.8f}f;", "}", ""]
    (root/"include/mkw/race_hud_layout.hpp").write_text("\n".join(lines))
    evidence = [ui/"ctrl/item_window_new.brctr",ui/"ctrl/lap_number.brctr",ui/"ctrl/position.brctr",
                ui/"blyt/item_window.brlyt",localized/"blyt/game_image_lap.brlyt",localized/"blyt/game_image_position.brlyt",
                ui/"blyt/go.brlyt",ui/"anim/go_fade_in.brlan",font]
    manifest["hudLayout"] = {"sourceCanvas":[832,456],"targetCanvas":[400,240],"uniformScale":scale,
                             "adaptation":"Uniform vertical scale; retain source left/right corner offsets at 5:3.",
                             "rectangles":rects,"countdownFrames":frames,
                             "sources":[{"path":str(p.relative_to(root)),"sha256":digest(p)} for p in evidence]}
    manifest["blooperPresentation"] = {"effect":ink_effect["name"], "definitionSource":str(reff_path.relative_to(root)),
                                       "definitionSha256":digest(reff_path),"halfExtent":ink_effect["size"],
                                       "cameraTranslation":ink_effect["translate"],"frames":ink_frames,
                                       "limitations":["Central source splat only; additional randomized droplet emitters and indirect UV distortion are not implemented.",
                                                       "Gameplay onset/duration is separate from the 450-frame source splat animation."]}
    manifest["countdownNote"] = "Original Race_U BMG strings, BRFNT glyphs and 70-frame BRLAN scale/alpha/color tracks. 60-tick numeral cadence follows native race countdown; GO begins on unlocked physics."
