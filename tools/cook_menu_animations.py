#!/usr/bin/env python3
"""Recover BRCTR page delays and bound BRLAN curves from the private Wii dump.

Page motion itself is procedural, recovered with MenuAudit.java from RMCU's
Page::ControlAnimator and UIControl::SetPositionAnim. A similarly named BRLAN
is not evidence that a control binds or plays it.
"""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'assets/menu-ui'


def text(data, offset):
    return data[offset:].split(b'\0', 1)[0].decode('ascii')


def f(value):
    return format(value, '.9g') + ('.f' if float(value).is_integer() else 'f')


def controller(path):
    data = path.read_bytes()
    table, strings = struct.unpack_from('>2H', data, 14)
    variants = []
    for i in range(struct.unpack_from('>H', data, table + 2)[0]):
        at = table + 12 + i * 60
        name, alpha, enabled, unknown, delay = struct.unpack_from('>4Hf', data, at)
        variants.append({'name': text(data, strings + name), 'alpha': alpha,
                         'enabled': bool(enabled), 'delay': delay})
    return variants


def sections(data):
    offset = struct.unpack_from('>H', data, 12)[0]
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset + 4)[0]
        if size < 8 or offset + size > len(data):
            raise ValueError('Invalid NW4R section')
        yield data[offset:offset + 4], offset, offset + 8
        offset += size


def layout_bindings(path):
    data = path.read_bytes()
    groups, materials, pane_materials = {}, [], {}
    for tag, block, start in sections(data):
        if tag == b'grp1':
            # Group members are fixed 16-byte names, not C strings. A name
            # occupying all 16 bytes must not consume the following section.
            groups[text(data, start)] = [data[start+20+i*16:start+36+i*16].split(b'\0',1)[0].decode('ascii')
                for i in range(struct.unpack_from('>H', data, start + 16)[0])]
        elif tag == b'mat1':
            materials = [text(data, block + struct.unpack_from('>I', data, start + 4 + i * 4)[0])
                for i in range(struct.unpack_from('>H', data, start)[0])]
        elif tag in (b'pic1', b'txt1', b'wnd1'):
            content = start + 68 if tag == b'pic1' else start + 56 if tag == b'txt1' else block + struct.unpack_from('>I', data, start + 88)[0]
            pane_materials[text(data, start + 4)] = materials[struct.unpack_from('>H', data, content + 16)[0]]
    return {group: set(members) | {pane_materials[name] for name in members if name in pane_materials}
            for group, members in groups.items()}


def animation(path):
    data = path.read_bytes()
    tracks = []
    for tag, block, start in sections(data):
        if tag != b'pai1':
            continue
        duration, loop, _, _, count, table = struct.unpack_from('>HBBHHI', data, start)
        for i in range(count):
            bind = block + struct.unpack_from('>I', data, block + table + 4 * i)[0]
            name = text(data, bind)
            for j in range(data[bind + 20]):
                anim = bind + struct.unpack_from('>I', data, bind + 24 + 4 * j)[0]
                kind = data[anim:anim + 4].decode('ascii')
                for k in range(data[anim + 4]):
                    track = anim + struct.unpack_from('>I', data, anim + 8 + 4 * k)[0]
                    index, target, curve = data[track:track + 3]
                    count = struct.unpack_from('>H', data, track + 4)[0]
                    first = track + struct.unpack_from('>I', data, track + 8)[0]
                    if curve not in (1, 2):
                        raise ValueError(f'Unsupported BRLAN curve {curve}: {path}')
                    fmt, stride = ('>3f', 12) if curve == 2 else ('>fHH', 8)
                    keys = [struct.unpack_from(fmt, data, first + n * stride) for n in range(count)]
                    if curve == 1:
                        keys = [(frame, value, 0.) for frame, value, _ in keys]
                    tracks.append({'element': name, 'kind': kind, 'target': target,
                                   'index': index, 'curve': curve, 'keys': keys})
        return duration, bool(loop), tracks
    raise ValueError('Missing pai1 animation section: ' + str(path))


def clips(path):
    data = path.read_bytes()
    offset, _, strings = struct.unpack_from('>3H', data, 12)
    def resource(folder, name):
        candidate = path.parent.parent / folder / name
        if not candidate.exists():
            root = UI if candidate.is_relative_to(UI) else ROOT / 'assets/race-ui'
            relative = candidate.relative_to(root)
            candidate = root / (relative.parts[0] + '_U') / Path(*relative.parts[1:])
        return candidate
    layout = resource('blyt', text(data, strings + struct.unpack_from('>H', data, 6)[0]) + '.brlyt')
    count = struct.unpack_from('>H', data, offset + 2)[0]
    if not count:
        return []
    members = layout_bindings(layout)
    table, result = offset + 8 + count * 8, []
    for i in range(count):
        group, panes, first, length = struct.unpack_from('>4H', data, offset + 8 + i * 8)
        group, panes = text(data, strings + group), text(data, strings + panes)
        for j in range(first, first + length):
            name, file, next_name, reverse, rate = struct.unpack_from('>4Hf', data, table + j * 12)
            source = resource('anim', text(data, strings + file))
            duration, loop, tracks = animation(source)
            result.append({'control': path.stem, 'group': group, 'name': text(data, strings + name),
                'next': text(data, strings + next_name), 'reverse': bool(reverse), 'rate': rate,
                'duration': duration, 'loop': loop, 'layout': str(layout.relative_to(ROOT)),
                'source': str(source.relative_to(ROOT)), 'panes': panes,
                'tracks': [track for track in tracks if not panes or track['element'] in members[panes]]})
    return result


def curve_header(clips):
    keys, tracks, key_offsets, track_offsets, entries = [], [], {}, {}, []
    for clip in clips:
        bound_tracks = []
        for track in clip['tracks']:
            key = tuple(tuple(k) for k in track['keys'])
            if key not in key_offsets:
                key_offsets[key] = len(keys)
                keys.extend(key)
            bound_tracks.append((track['element'], track['kind'], track['target'], track['index'],
                                 track['curve'], key_offsets[key], len(key)))
        bound_tracks = tuple(bound_tracks)
        if bound_tracks not in track_offsets:
            track_offsets[bound_tracks] = len(tracks)
            tracks.extend(bound_tracks)
        entries.append((clip, track_offsets[bound_tracks], len(bound_tracks)))
    lines = ['struct UiAnimationKey {float frame,value,tangent;};',
             'inline constexpr UiAnimationKey UiAnimationKeys[]={']
    lines.extend('{' + ','.join(f(v) for v in key) + '},' for key in keys)
    lines += ['};',
        'struct UiAnimationTrack {const char* element;const char* kind;unsigned target,index,curve,firstKey,keyCount;};',
        'inline constexpr UiAnimationTrack UiAnimationTracks[]={']
    lines.extend('{' + ','.join([json.dumps(t[0]), json.dumps(t[1]), *(str(v) for v in t[2:])]) + '},' for t in tracks)
    lines += ['};',
        'struct UiAnimationClip {const char* control;const char* group;const char* name;const char* next;float duration,rate;bool reverse,loop;unsigned firstTrack,trackCount;};',
        'inline constexpr UiAnimationClip UiAnimationClips[]={']
    for clip, offset, count in entries:
        lines.append('{' + ','.join([*(json.dumps(clip[k]) for k in ('control', 'group', 'name', 'next')),
            f(clip['duration']), f(clip['rate']), str(clip['reverse']).lower(), str(clip['loop']).lower(),
            str(offset), str(count)]) + '},')
    lines += ['};',
        'inline const UiAnimationClip* uiAnimationClip(const char* control,const char* group,const char* name){',
        ' for(const auto& clip:UiAnimationClips)if(!std::strcmp(clip.control,control)&&!std::strcmp(clip.group,group)&&!std::strcmp(clip.name,name))return &clip;',
        ' return nullptr;', '}',
        '// PushButton::GetAnimationFrameSize805b62e4 divides by both rates.',
        'inline float uiButtonConfirmFrames(const char* control,float groupRate=1.f){',
        ' const auto* clip=uiAnimationClip(control,"OK","OK");',
        ' return clip&&clip->rate>0&&groupRate>0?clip->duration/clip->rate/groupRate:0.f;', '}',
        '// Hermite tangent is value per source frame, not normalized interval.',
        'inline float uiAnimationSample(const UiAnimationTrack& track,float frame){',
        ' if(!track.keyCount)return 0.f;const auto* keys=UiAnimationKeys+track.firstKey;',
        ' if(frame<keys[0].frame)return keys[0].value;',
        ' for(unsigned i=1;i<track.keyCount;++i)if(frame<keys[i].frame){',
        '  const auto& a=keys[i-1];const auto& b=keys[i];if(track.curve==1)return a.value;',
        '  const float span=b.frame-a.frame,t=(frame-a.frame)/span,t2=t*t,t3=t2*t;',
        '  return (2*t3-3*t2+1)*a.value+(t3-2*t2+t)*a.tangent*span+(-2*t3+3*t2)*b.value+(t3-t2)*b.tangent*span;',
        ' }return keys[track.keyCount-1].value;', '}',
        '// Frame is the BRLAN source frame. Playback rate/next/reverse are retained',
        '// on each clip so group playback can follow its native control state.',
        'inline float uiAnimationValue(const UiAnimationClip& clip,const char* element,const char* kind,unsigned target,float frame,float fallback,unsigned index=0){',
        ' for(unsigned i=0;i<clip.trackCount;++i){const auto& t=UiAnimationTracks[clip.firstTrack+i];',
        '  if(t.target==target&&t.index==index&&!std::strcmp(t.element,element)&&!std::strcmp(t.kind,kind))return uiAnimationSample(t,frame);',
        ' }return fallback;', '}',
    ]
    return lines, {'clips': len(entries), 'tracks': len(tracks), 'keys': len(keys)}


def main():
    variants, sources, animations = {}, {}, {}
    from verify_menu_retail import EXPECTED_HASH
    rel = ROOT / 'build/itembox-retail.rel'
    if hashlib.sha256(rel.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError('Page timing requires the audited original RMCU executable')
    sources[str(rel.relative_to(ROOT))] = EXPECTED_HASH
    controller_paths = [path for archive in ['Title', 'MenuSingle'] for path in sorted((UI / archive).rglob('*.brctr'))]
    controller_paths += [UI / 'MenuOther' / part / 'ctrl' / (name + '.brctr') for part, name in [('button', 'LicenseManagementButton'), ('control', 'LicenseManagement'), ('control','Record'), ('button','NextRecordButton')]]
    controller_paths += [UI / 'Channel/button/ctrl/OptionTopButton.brctr']
    controller_paths += [UI / 'Present/common/ctrl/NextButton.brctr']
    controller_paths += sorted((UI / 'Present/control/ctrl').glob('*.brctr'))
    controller_paths += [ROOT / 'assets/race-ui/Race/button/ctrl' / (name + '.brctr')
                         for name in ['PauseMenuVS', 'PauseMenuGrandPrix', 'PauseMenuTimeAttack', 'PauseMenuBT']]
    for path in controller_paths:
        if path.name.startswith('._'):
            continue
        sources[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
        for variant in controller(path):
            variants[path.stem, variant['name']] = variant
        for clip in clips(path):
            animations[clip['control'], clip['group'], clip['name']] = clip
            for source in (clip['source'], clip['layout']):
                if source not in sources:
                    sources[source] = hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
    header = [
        '// Generated by tools/cook_menu_animations.py from original retail resources.',
        '#pragma once', '#include <algorithm>', '#include <cmath>', '#include <cstring>',
        'namespace mkw {',
        'struct UiControlMotion {const char* control;const char* variant;float delay;bool enabled;};',
        'inline constexpr UiControlMotion UiControlMotions[]={',
    ]
    for (name, variant), value in sorted(variants.items()):
        header.append('{%s,%s,%s,%s},' % (json.dumps(name), json.dumps(variant),
                                        f(value['delay']), str(value['enabled']).lower()))
    header += [
        '};',
        'inline const UiControlMotion* uiControlMotion(const char* control,const char* variant){',
        ' for(const auto& value:UiControlMotions)if(!std::strcmp(value.control,control)&&!std::strcmp(value.variant,variant))return &value;',
        ' return nullptr;', '}',
        '// Native Page::ControlAnimator, USA805ddecc; duration from USA808901c0.',
        'inline constexpr float UiPageMotionFrames=10.f;',
        'inline float uiPageTransitionProgress(float age,float delay,float maxDelay,bool entering,bool reverse,float exitHold=0.f){',
        ' delay=reverse?maxDelay-delay:delay;',
        ' const float time=age-(entering?0.f:exitHold);',
        ' const float start=entering?(reverse?1.f:-1.f):0.f;',
        ' const float delta=reverse?-1.f:1.f;',
        ' return start+delta*std::clamp((time-delay)/UiPageMotionFrames,0.f,1.f);', '}',
        '// Input progress is signed; only ordinary controls move horizontally.',
        'enum class UiMotionKind {Slide,Back,PageTitle,Instruction,ObiTop,ObiBottom,BlackBack};',
        'struct UiMotionTransform {float x=0,y=0,alpha=1;};',
        'inline UiMotionTransform uiControlTransition(UiMotionKind kind,float progress,bool mainMenu=false,float parentScaleX=1.f,float sectionWidth=608.f){',
        ' const float magnitude=std::abs(progress);UiMotionTransform value;',
        ' switch(kind){',
        ' case UiMotionKind::Slide:value.x=-sectionWidth*progress*parentScaleX;break;',
        ' case UiMotionKind::Back:value.y=mainMenu?-100.f*magnitude:0.f;value.alpha=std::floor(255.f*(1.f-magnitude))/255.f;break;',
        ' case UiMotionKind::PageTitle:value.y=mainMenu?100.f*magnitude:0.f;value.alpha=std::floor(255.f*(1.f-progress*progress))/255.f;break;',
        ' case UiMotionKind::Instruction:value.alpha=std::floor(255.f*(1.f-progress*progress))/255.f;break;',
        ' case UiMotionKind::ObiTop:value.y=100.f*magnitude;value.alpha=std::floor(255.f*(1.f-progress*progress))/255.f;break;',
        ' case UiMotionKind::ObiBottom:value.y=-100.f*magnitude;value.alpha=std::floor(255.f*(1.f-progress*progress))/255.f;break;',
        ' case UiMotionKind::BlackBack:value.alpha=std::floor(255.f*(1.f-magnitude))/255.f;break;',
        ' }return value;', '}',
    ]
    curve_lines, counts = curve_header(list(animations.values()))
    header += curve_lines + ['}']
    output = ROOT / 'include/mkw/menu_animations.hpp'
    output.write_text('\n'.join(header) + '\n')
    manifest = {'version': 1, 'sourceHashes': sources,
                'motion': {'duration': 10, 'rate': 60, 'sourceFunction': '0x805ddecc',
                           'sourceDuration': '0x808901c0', 'positionFunction': '0x8060bd74'},
                'controls': [{**value, 'control': name} for (name, _), value in sorted(variants.items())],
                'animations': list(animations.values())}
    receipt = ROOT / 'sd-card/3ds/mkw-3ds/models/menu-animations.json'
    receipt.write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'controls': len(variants), 'sources': len(sources), **counts, 'header': str(output.relative_to(ROOT))}))


if __name__ == '__main__':
    main()
