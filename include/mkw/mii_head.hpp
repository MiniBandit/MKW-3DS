#pragma once
#include <mkw/mii_appearance.hpp>
#include <mkw/mii_part_bank.hpp>
namespace mkw {
// Original RFL appearance, not a lossy conversion of a newer native Mii.
// faceFeature is the original combined wrinkle/makeup resource selector.
struct RflHeadSpec {MiiAppearance appearance;unsigned faceFeature=0;};
// Exact common subset only; unsupported native styles are never approximated.
bool nativeMiiHeadSpec(const NativeMii&,RflHeadSpec&);
struct MiiHeadPart {
 MiiPartShape shape;
 MiiPartTexture texture;
 bool mirrored=false;
};
struct MiiHead {
 std::array<MiiHeadPart,9> parts;
 // Shared opaque/transparent retail part identifiers; renderer applies the
 // original material recipe rather than treating all parts as plain RGBA.
 unsigned skinColor=0,hairColor=0,beardColor=0,glassesColor=0,favoriteColor=0;
};
// Build only the chosen resources and compose the mask once per identity.
// Reject unsupported appearance; never silently replace a newer Mii's parts.
bool buildMiiHead(const MiiPartBank&,const RflHeadSpec&,unsigned maskResolution,MiiHead&);
}
