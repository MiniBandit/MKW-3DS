#include "mkw/race_items.hpp"
#include <cassert>
#include <cstdio>

using namespace mkw;
static ItemSurface flatGround(Vec3, Vec3 p, float radius) {
    ItemSurface s{};
    if (p.y < radius) { s.correction.y = radius - p.y; s.floorNormal = {0, 1, 0}; }
    return s;
}
static void step(RaceItems& items, ItemRacer& racer, bool use = false) {
    ItemInput input{}; input.use = use;
    items.beforeStep(&racer, &input, 1, true);
    items.afterStep(&racer, 1, flatGround);
}
static unsigned breaks(const RaceItems& items, ItemKind kind) {
    unsigned n = 0;
    for (const auto& e : items.events())
        if (e.effect == ItemEffect::Break && e.frames == unsigned(kind)) ++n;
    return n;
}
static unsigned breakSounds(const RaceItems& items, ItemKind kind) {
    unsigned n = 0;
    for (const auto& s : items.sounds())
        if (s.cue == ItemSoundCue::Break && s.kind == kind) ++n;
    return n;
}
static void check(ItemKind kind, bool scattered) {
    RaceItems items; items.reset({}, 1);
    ItemRacer racer{}; racer.grounded = true;
    assert(items.give(0, kind));
    if (scattered) {
        // Exercise actual loss/scatter production code, not direct actor mutation.
        ItemInput input{};
        items.beforeStep(&racer, &input, 1, true);
        assert(items.karts().lightning(0, 60, false, false));
        items.afterStep(&racer, 1, flatGround);
    } else {
        step(items, racer, true); // equip
        step(items, racer);       // release the held single
    }
    unsigned slot = RaceItems::MaxActors;
    for (unsigned wait = 0; wait < 12; ++wait) {
        for (unsigned i = 0; i < items.actors().size(); ++i)
            if (items.actors()[i].active && items.actors()[i].kind == kind) slot = i;
        if (slot != RaceItems::MaxActors) break;
        step(items, racer);
    }
    assert(slot != RaceItems::MaxActors);
    const ItemActor& actor = items.actors()[slot];
    assert(actor.scattered == scattered);
    const unsigned expires = scattered ? 1800 : kind == ItemKind::GreenShell ? 1501 : 2401;
    assert(actor.lifetime == expires);
    // No kart contact while testing the independent lifetime path.
    racer.unavailable = true;
    while (actor.age < expires - 1) {
        assert(actor.active);
        step(items, racer);
        assert(breaks(items, kind) == 0 && breakSounds(items, kind) == 0);
    }
    assert(actor.active && actor.age == expires - 1);
    const Vec3 endPosition = actor.position;
    step(items, racer);
    assert(!actor.active && actor.age == expires);
    assert(breaks(items, kind) == unsigned(!scattered));
    // Existing scattered-Green sound policy is intentionally preserved; Red is silent.
    assert(breakSounds(items, kind) == unsigned(!scattered || kind == ItemKind::GreenShell));
    if (!scattered) for (const auto& e : items.events()) if (e.effect == ItemEffect::Break) {
        assert(length(e.direction - endPosition) < .0001f);
        assert(e.scale == actor.scale);
    }
    step(items, racer);
    assert(breaks(items, kind) == 0 && breakSounds(items, kind) == 0);
    std::printf("%s %s expiry=%u: PASS\n", itemName(kind), scattered ? "scattered" : "launched", expires);
}
int main() {
    for (auto kind : {ItemKind::GreenShell, ItemKind::RedShell}) {
        check(kind, false);
        check(kind, true);
    }
}
