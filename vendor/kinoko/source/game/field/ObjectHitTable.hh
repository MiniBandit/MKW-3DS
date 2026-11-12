#pragma once

#include "game/field/obj/ObjectId.hh"

#include "game/kart/KartCollide.hh"

#include <span>

namespace Kinoko::Field {

class ObjectHitTable {
public:
    ObjectHitTable(const char *filename);
    ~ObjectHitTable();

    /// @param column Retail per-row field: 0 normal, 1 Star, 2 Mega, 3 Bullet (kart tables).
    Kart::Reaction reaction(s16 i, s16 column = 0) const;
    s16 slot(ObjectId id) const;

    /// RMCU 0x808179cc: the kart-table column for a racer's power-up state.
    static s16 KartColumn(const Kart::KartObject *kartObj);

private:
    s16 m_count;
    s16 m_fieldCount;
    owning_span<s16> m_reactions; ///< m_count rows of m_fieldCount reactions.
    const s16 *m_slots;
};

} // namespace Kinoko::Field
