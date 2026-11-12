"""Periodic gutter for the native scrolling stripe background (not other UI)."""
from PIL import Image
from pathlib import Path
import subprocess
from devkitpro import tool

def write_wrapped_background(content, output):
    width,height=content.size
    tw=max(8,1<<(width-1).bit_length());th=max(8,1<<(height+1).bit_length())
    if th!=max(8,1<<(height-1).bit_length()):
        raise ValueError('Background gutter would change its native texture dimensions')
    # The authored background is horizontal stripes; wrapping horizontally
    # also fills opaque POT columns without changing any displayed content.
    image=Image.new('RGBA',(tw,th))
    for y in range(th):
        row=content.crop((0,(y-1)%height,width,(y-1)%height+1))
        for x in range(0,tw,width):image.paste(row,(x,y))
    target=Path(output)/'textures/ui_background.png';target.parent.mkdir(parents=True,exist_ok=True);image.save(target)
    subprocess.run([str(tool('tex3ds')),'-f','rgba8','-z','none','-o',str(target.with_suffix('.t3x')),str(target)],check=True)
