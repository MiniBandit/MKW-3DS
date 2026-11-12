"""Preserve original ceremony EGG lighting, fog and bloom parameters."""
from pathlib import Path
import json
from cook_course_lighting import ROOT, LIGHTS, RETAIL_TEXTURE_LIGHTS, parse_blight, parse_blmap, parse_bfg, header
from cook_course_bloom import parse as parse_bloom


def cook_lighting(source):
    lights, blooms = [], []
    for name in ('winningrun_demo', 'loser_demo', 'draw_demo', 'ending_demo'):
        folder = source / 'unpacked' / name / 'posteffect'
        row = parse_blight((folder / 'posteffect.blight').read_bytes())
        row['textures'] = parse_blmap((folder / 'posteffect.blmap').read_bytes())['textures']
        fog = folder / 'posteffect.bfg'
        row['fog'] = parse_bfg(fog.read_bytes()) if fog.exists() else []
        if len(row['lights']) != LIGHTS or len(row['ambients']) != LIGHTS:
            raise ValueError('Unexpected ceremony light count: ' + name)
        if any(l['type'] != 1 for l in row['lights'] if l['flags'] & 1):
            raise ValueError('Unsupported ceremony light type: ' + name)
        if [t['name'] for t in row['textures']] != ['lm_0', 'lm_1'] or any(
                t['type'] != 0 or len(t['entries']) > RETAIL_TEXTURE_LIGHTS for t in row['textures']):
            raise ValueError('Unsupported ceremony light texture: ' + name)
        lights.append((name, row))
        blooms.append((name, parse_bloom((folder / 'posteffect.bblm').read_bytes())))
    output = header(lights).replace('CourseLightings', 'AwardLightings').replace('courseLighting(', 'awardLighting(')
    output = output.replace('tools/cook_course_lighting.py', 'tools/cook_award_lighting.py')
    output += '\n#include <mkw/course_bloom.hpp>\nnamespace mkw {\ninline constexpr CourseBloom AwardBlooms[]={\n'
    f = lambda v: format(v, '.9g') + ('.f' if float(v).is_integer() else 'f')
    for name, p in blooms:
        output += '{' + ','.join([json.dumps(name), f(p['threshold']), hex(p['thresholdColor'])+'u',
            hex(p['tint'])+'u', str(p['flags']),
            '{'+','.join(f(v[0]) for v in p['blur'][:2])+'}',
            '{'+','.join(f(v[1]) for v in p['blur'][:2])+'}',
            '{'+','.join(f(v) for v in p['weights'])+'}']) + '},\n'
    output += '};\n}\n'
    (ROOT / 'include/mkw/award_lighting.hpp').write_text(output)


if __name__ == '__main__':
    cook_lighting(ROOT / 'build/award-source')
