"""Read-only RMCU StaticR relocations for source comparisons.

The analysis image uses the same file-offset mapping as verify_menu_retail.
No original file is modified. Imports from main.dol (module 0) are absolute;
module 1 targets refer to this REL's sections. Other modules are rejected.
"""
import struct

BASE = 0x8050bf60


def relocated(path):
    data = bytearray(path.read_bytes())
    module = struct.unpack_from('>I', data)[0]
    count, table = struct.unpack_from('>II', data, 12)
    sections = [struct.unpack_from('>II', data, table + i * 8) for i in range(count)]
    imports, size = struct.unpack_from('>II', data, 0x28)
    for entry in range(imports, imports + size, 8):
        target_module, stream = struct.unpack_from('>II', data, entry)
        if target_module not in (0, module):
            raise ValueError('Unsupported imported module %d' % target_module)
        section = offset = 0
        while True:
            delta, kind, target_section, addend = struct.unpack_from('>HBBI', data, stream)
            stream += 8
            offset += delta
            if kind == 203:  # R_DOLPHIN_END
                break
            if kind == 202:  # R_DOLPHIN_SECTION
                section, offset = target_section, 0
                continue
            if kind in (0, 201):  # R_PPC_NONE / R_DOLPHIN_NOP
                continue
            destination = (sections[section][0] & ~3) + offset
            if not (sections[section][0] & ~3):
                raise ValueError('Relocation into BSS is not a file comparison')
            value = addend if target_module == 0 else BASE + (sections[target_section][0] & ~3) + addend
            value &= 0xffffffff
            if kind == 1:  # R_PPC_ADDR32
                struct.pack_into('>I', data, destination, value)
            elif kind in (3, 4):  # ADDR16 / ADDR16_LO
                struct.pack_into('>H', data, destination, value & 65535)
            elif kind in (5, 6):  # ADDR16_HI / ADDR16_HA
                struct.pack_into('>H', data, destination, ((value + (0x8000 if kind == 6 else 0)) >> 16) & 65535)
            elif kind in (2, 10):  # ADDR24 / REL24, preserve opcode and AA/LK
                field = value if kind == 2 else value - (BASE + destination)
                instruction = struct.unpack_from('>I', data, destination)[0]
                struct.pack_into('>I', data, destination, (instruction & 0xfc000003) | (field & 0x03fffffc))
            elif kind == 26:  # REL32
                struct.pack_into('>I', data, destination, (value - BASE - destination) & 0xffffffff)
            else:
                raise ValueError('Unsupported PPC relocation %d' % kind)
    return data


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('start', type=lambda s: int(s, 0))
    parser.add_argument('end', type=lambda s: int(s, 0))
    args = parser.parse_args()
    image = relocated(args.source)
    disassembler = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
    disassembler.skipdata = True
    for instruction in disassembler.disasm(image[args.start - BASE:args.end - BASE], args.start):
        print('%08x: %s %s' % (instruction.address, instruction.mnemonic, instruction.op_str))
