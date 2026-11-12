"""Reduce supported original GX alpha equations and ROP state for GMDL v3/v4.

Old assets keep legacy flags. New assets carry explicit blend/depth state and an
alpha mode in unused flag bits; unsupported alpha equations stay documented.
State the flags cannot hold goes in the submesh's animationFrames bits 16..31
(frame counts stay below 65536): bits 16..23 an exact alpha-test reference,
24..27 and 28..31 the PICA source/destination blend factor plus one.
"""
from collections import defaultdict


def add(a, b, sign=1):
    out = defaultdict(float, a)
    for key, value in b.items():
        out[key] += value * sign
    return {k: v for k, v in out.items() if abs(v) > 1e-8}


def mul(a, b):
    out = defaultdict(float)
    for ka, va in a.items():
        for kb, vb in b.items():
            out[tuple(sorted(ka + kb))] += va * vb
    return {k: v for k, v in out.items() if abs(v) > 1e-8}


def alpha_expression(material, primary, secondary, constants=None):
    """The final alpha as a polynomial, and the last stage's alpha scale.
    constants maps a texture map index to the alpha a course rule replaced it with."""
    registers = [{(): c['a']} for c in material['colorRegisters']]
    stages = material['gxMaterial']['tevStages']
    for index, stage in enumerate(stages):
        if stage['alphaOp'] != 0 or stage['alphaBias'] != 0 or (stage['alphaScale'] != 0 and index != len(stages)-1):
            return None, 1
        selector = stage['konstAlphaSel']
        if selector < 8:
            konst = (8-selector)/8
        elif 16 <= selector < 32:
            konst = material['colorConstants'][selector%4]['rgba'[(selector-16)//4]]
        else:
            return None, 1
        texture = stage['texMap']
        tex = 'p' if texture == primary else 'q' if texture == secondary else 't'+str(texture)
        fixed = (constants or {}).get(str(texture))
        # noclip retains the BP raster selector, not GX ColorChannelID.
        # Selector 7 means zero even when the channel's material alpha is 1.
        # Alpha bump and the second raster channel need a separate runtime path.
        channel = stage['channelId']
        if 5 in [stage['alphaIn'+letter] for letter in 'ABCD'] and channel not in (0,7):
            return None, 1
        raster = {} if channel == 7 else {('v',): 1}
        values = registers + [{(): fixed} if fixed is not None else {(tex,): 1}, raster, {(): konst}, {}]
        a,b,c,d = [values[stage['alphaIn'+letter]] for letter in 'ABCD']
        expression = add(add(a, mul(add(b,a,-1), c)), d)
        # A clamped non-negative sum with a constant >=1 is identically one
        # (menu vehicle materials often add konst 1 to APREV).
        if stage['alphaClamp'] and expression.get((),0) >= 1 and all(v >= 0 for v in expression.values()):
            expression = {(): 1.}
        # Only fold expressions whose intermediate range never needs saturation.
        # Products have factors in [0,1], so nonnegative coefficient sum <=1 is safe.
        if any(v < -1e-8 for v in expression.values()) or sum(expression.values()) > 1.000001:
            return None, 1
        registers[stage['alphaRegId']] = expression
    return registers[0], ((1, 2, 4, .5)[stages[-1]['alphaScale']] if stages else 1)


# GX blend factors (source and destination tables differ) to PICA GPU_BLENDFACTOR.
PICA_SOURCE = {0:0, 1:1, 2:4, 3:5, 4:6, 5:7, 6:8, 7:9}
PICA_DESTINATION = {0:0, 1:1, 2:2, 3:3, 4:6, 5:7, 6:8, 7:9}


def material_state(shape):
    """Return flags, alpha multiplier, preserve vertex alpha, any limitation,
    the alpha mix and the extended animationFrames bits."""
    flags = shape['flags']
    material = shape.get('sourceMaterial')
    if material is None:
        return flags, 1., True, None, None, 0
    extended = 0
    limitation = None
    # Cutouts are GX render state, not inferred from transparent PNG pixels.
    # Match the full two-comparison truth table to a single PICA comparison.
    # Four references fit bits 29..30 (including the question mark's 165
    # cutoff); any other exact reference uses the extended bits.
    test = material['gxMaterial']['alphaTest']
    compare = lambda op,a,b: (False,a<b,a==b,a<=b,a>b,a!=b,a>=b,True)[op]
    accepted=[]
    for value in range(256):
        a=compare(test['compareA'],value,round(test['referenceA']*255))
        b=compare(test['compareB'],value,round(test['referenceB']*255))
        accepted.append((a and b,a or b,a!=b,a==b)[test['op']])
    flags &= ~(1 | (31<<26))
    if not all(accepted):
        table = lambda op,reference: [compare(op,value,reference) for value in range(256)]
        legacy=[(op,index) for index,reference in enumerate((0,128,165,255)) for op in range(8) if accepted==table(op,reference)]
        exact=[(op,reference) for reference in range(1,255) for op in range(8) if accepted==table(op,reference)]
        if legacy:
            op,index=legacy[0];flags |= 1 | (op<<26) | (index<<29)
        elif exact:
            op,reference=exact[0];flags |= 1 | (op<<26);extended |= reference<<16
        else:
            # Two-sided tests (sparkle thresholds): the single comparison that
            # misclassifies the least alpha, weighting each value by itself.
            cost,op,reference=min((sum(value for value in range(256) if accepted[value]!=compare(op,value,reference)),op,reference)
                                  for op in range(1,7) for reference in range(1,255))
            flags |= 1 | (op<<26);extended |= reference<<16
            limitation='Approximated original GX alpha test'
    rop = material['gxMaterial']['ropInfo']
    blend = rop['blendMode']
    if blend not in (0,1):
        return flags, 1., True, 'Unsupported original GX blend equation', None, extended
    flags &= ~(2 | 64 | 128 | (7<<20) | (1<<23) | (1<<24))
    flags |= 1<<31
    if blend == 1:
        flags |= 2
        source,destination=rop['blendSrcFactor'],rop['blendDstFactor']
        if source in (1,4) and destination in (1,5):
            if destination == 1: flags |= 64
            if source == 1: flags |= 128
        else:
            # Multiplicative and screen blends (sea tint, aurora ribbon).
            extended |= (PICA_SOURCE[source]+1)<<24 | (PICA_DESTINATION[destination]+1)<<28
    if rop['depthTest']: flags |= 1<<23
    if rop['depthWrite']: flags |= 1<<24
    reflection = shape.get('reflection') or {}
    if reflection.get('mode') == 25:
        # Evaluate the original fountain's clamped intermediate alpha on PICA.
        return flags, 1., True, None, None, extended
    stages = material['gxMaterial']['tevStages']
    if reflection.get('mode') == 18 and len(stages) == 2 and \
            [stages[0]['alphaIn'+c] for c in 'ABCD'] == [4,7,7,7] and \
            [stages[1]['alphaIn'+c] for c in 'ABCD'] == [4,7,7,0] and \
            all(s['alphaOp'] == 0 and s['alphaBias'] == 0 and s['alphaScale'] == 0 and s['alphaClamp'] and s['alphaRegId'] == 0 for s in stages) and \
            stages[0]['texMap'] == shape['primaryMap'] and stages[1]['texMap'] == shape['secondaryMap']:
        # The core's clamped texture-alpha sum is set by mode18 itself.
        return flags, 1., True, limitation, None, extended
    if reflection.get('mode') == 11 and len(stages) == 2 and \
            [stages[0]['alphaIn'+c] for c in 'ABCD'] == [7,4,5,6] and \
            [stages[1]['alphaIn'+c] for c in 'ABCD'] == [0,7,7,7] and \
            all(s['alphaOp'] == 0 and s['alphaBias'] == 0 and s['alphaScale'] == 0 and s['alphaClamp'] and s['alphaRegId'] == 0 for s in stages) and \
            stages[0]['texMap'] == shape['secondaryMap'] and stages[0]['channelId'] == 0 and \
            16 <= stages[0]['konstAlphaSel'] < 32:
        # Sherbet: clamp(projected glow alpha * raster alpha + K0.a),
        # retained by stage1. This sum saturates and cannot be reduced to the
        # polynomial path below. Mode7 preserves that saturation on PICA.
        selector = stages[0]['konstAlphaSel']
        weight = material['colorConstants'][selector%4]['rgba'[(selector-16)//4]]
        return flags|(7<<20), 1., True, limitation, round(weight*255), extended
    expression, alpha_scale = alpha_expression(material, shape['primaryMap'], shape['secondaryMap'], shape.get('constantTextures'))
    if reflection.get('mode') == 14 and reflection.get('constant', 0) & 1:
        # Raster alpha is a colour weight; retain it independently of opacity.
        if expression != {('p','q'): 1.} or alpha_scale not in (1, 2):
            raise ValueError('Boble raster-alpha variant needs texture-product opacity')
        return flags|((6 if alpha_scale == 2 else 3)<<20), 1., True, limitation, None, extended
    modes = {('p',):1, ('q',):2, ('p','q'):3, ():4}
    if alpha_scale != 1:
        if alpha_scale == 2 and expression == {('p',): 1.} and shape['secondaryMap'] < 0:
            # Single-map additive fire planes: mode6's missing secondary is
            # the renderer's white texture, giving exactly clamp(2*T0.a).
            return flags|(6<<20), 1., False, limitation, None, extended
        # Doubled texture product (sea sparkles): alpha mode 6.
        if alpha_scale == 2 and expression == {('p','q'): 1.}:
            return flags|(6<<20), 1., False, limitation, None, extended
        # A final GX divide-by-two is exact for a nonnegative monomial:
        # move its scale into the cooked vertex multiplier. Texture alpha is
        # still sampled at runtime; additive road overlays use raster alpha.
        # Scales that can saturate must retain their separate runtime mode.
        if expression is None or len(expression)>1 or sum(expression.values())*alpha_scale>1:
            return flags, 1., True, 'Unsupported original GX alpha equation', None, extended
        expression={factors:weight*alpha_scale for factors,weight in expression.items()}
    if reflection.get('mode') in (10,30) and expression and set(expression)=={('p','v'),('q','v')} and abs(sum(expression.values())-1)<1e-6:
        # Background trophy wood/balloon: mixed texture opacity multiplied by
        # the independent raster alpha. Preserve that last multiply on PICA.
        return flags|(5<<20), 1., True, limitation, round(expression[('p','v')]*255), extended
    if expression and set(expression)=={('p',),('q',)} and abs(sum(expression.values())-1)<1e-6:
        # Colour mode 2 (without the constant lerp) weights the secondary texture
        # by the constant's alpha, so it cannot also carry the alpha mix.
        if (flags>>12)&3 == 2 and not flags&(1<<25):
            return flags, 1., True, 'Unsupported original GX alpha equation', None, extended
        return flags|(5<<20), 1., False, limitation, round(expression[('p',)]*255), extended
    if expression is None or len(expression)>1:
        return flags, 1., True, 'Unsupported original GX alpha equation', None, extended
    factors, weight = next(iter(expression.items())) if expression else ((),0.)
    vertex = 'v' in factors
    factors = tuple(f for f in factors if f!='v')
    if factors not in modes or sum(f=='v' for f in next(iter(expression),()))>1:
        return flags, 1., True, 'Unsupported original GX alpha equation', None, extended
    flags |= modes[factors]<<20
    return flags, weight, vertex, limitation, None, extended


# GMDL submesh records: v3 (first, count, texture, flags, secondary, constant,
# uv velocity x4, animationFrames) and v4, which appends the retail course
# lighting word, the material ambient register and the constant added by a
# light-texture albedo stage (include/mkw/retail_lighting.hpp).
GMDL_SUBMESH = {3: '<IIiIiI4fI', 4: '<IIiIiI4fIIII', 5: '<IIiIiI4fIIII3I6f'}
GMDL_SUBMESH_BYTES = {3: 44, 4: 56, 5: 92}
RETAIL_LIGHT_SETS = 8


def retail_lighting(shape):
    """Validate a shape's retail lighting state (tools/retail_lighting.ts) and
    return the GMDL v4 (word, ambient, add) fields. Shapes without it stay
    legacy (0, 0, 0)."""
    retail = shape.get('retail')
    if not retail:
        return [0, 0, 0]
    word, ambient, add = retail['word'], retail['ambient'], retail.get('add', 0)
    if not word & 1 or word >> 25:
        raise ValueError('Invalid retail lighting word: ' + shape['material'])
    channel = word >> 2 & 3
    if word & (1 << 24) and (channel != 2 or word & ((1 << 17) | (1 << 20))):
        raise ValueError("Invalid Mii albedo tint: " + shape["material"])
    if channel == 3 or (channel == 0 and word & ~(3 | (1 << 21) | (1 << 22) | (1 << 23))):
        raise ValueError('Invalid retail lighting channel: ' + shape['material'])
    # Renderer::retailLight takes the ambient from the register; no retail
    # course material sources it from the vertex colour.
    if word >> 10 & 1:
        raise ValueError('Vertex ambient source is not implemented: ' + shape['material'])
    if channel == 2 and not (word >> 11 & 3 or word >> 15 & 3):
        raise ValueError('Light-texture channel without a light texture: ' + shape['material'])
    if not 0 <= ambient <= 0xffffffff:
        raise ValueError('Invalid ambient register: ' + shape['material'])
    # Bit 20: the albedo stage adds a constant (GX C1); only with light textures.
    if bool(word >> 20 & 1) != bool(add) or (add and channel != 2) or not 0 <= add <= 0xffffffff:
        raise ValueError('Invalid albedo add: ' + shape['material'])
    return [word, ambient, add]
