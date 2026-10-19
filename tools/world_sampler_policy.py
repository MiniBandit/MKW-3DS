"""Narrow physical-console LOD0 exceptions already accepted in HW15.

The source material and geometry audit selected these drivable mesh surfaces,
plus Moonview's shared foliage/fence cutout atlas. Decorative fences, CPU models,
other course textures and secondary samplers retain their authored settings.
"""
PRIMARY_LOD0={
 'course_04':{'ami'}, 'course_12':{'ami'}, 'course_15':{'WT_fence'},
 'course_18':{'wl_ami1_v','wl_ami1'}, 'course_30':{'b_ami'},
 'course_10':{'Nuki_Ryoumen'},
}
def world_samplers(prefix,shape,rows):
 selected=shape.get('material') in PRIMARY_LOD0.get(prefix,set())
 selected|=prefix.startswith('obj11_ami__') and shape.get('material')=='ami_m'
 if not selected:return rows
 bias,minimum,magnify,valid,reserved=rows[0]
 if not valid:raise ValueError('Missing source sampler on accepted LOD0 surface')
 return ((bias,1,magnify,valid,reserved),*rows[1:])
