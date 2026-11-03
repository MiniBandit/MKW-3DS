#include "mkw/race_items.hpp"
#include "mkw/driver_item_signals.hpp"
#include <cassert>
#include <cstdio>

// Exercise the actual holder, guidance, and kart-contact updates together.
// Flat ground supplies correction only when the sphere intersects it.
static mkw::ItemSurface floorQuery(mkw::Vec3, mkw::Vec3 p, float radius) {
    mkw::ItemSurface result{};
    if (p.y < radius) {
        result.correction.y = radius - p.y;
        result.floorNormal = {0, 1, 0};
    }
    return result;
}

int main() {
    using namespace mkw;
    // The original Red starts requesting a glance strictly after tick 20.
    ItemActor red{};
    red.active=true; red.kind=ItemKind::RedShell; red.target=0;
    red.guidance.phase=4; red.guidance.ticks=20;
    Vec3 target{};
    assert(!driverItemLookTarget(red,0,target));
    red.guidance.ticks=21;
    assert(driverItemLookTarget(red,0,target));
    assert(!driverItemLookTarget(red,1,target));
    red.explosion=true;
    assert(!driverItemLookTarget(red,0,target));
    for (float speed : {0.f, 70.f}) {
        RaceItems items;
        items.reset({}, 2);
        ItemRacer racers[2]{};
        racers[0].position = {0, 0, 3000};
        racers[0].progress = 1;
        for (auto& r : racers) {
            r.speed = speed;
            r.speedRatio = speed / 100.f;
            r.haveVelocity = true;
            r.velocity = {0, 0, speed};
        }
        assert(items.give(1, ItemKind::BlueShell));
        unsigned phases[6]{}, explosionFrame = 0;
        bool left = false, right = false;
        for (unsigned frame = 0; frame < 600; ++frame) {
            ItemInput inputs[2]{};
            inputs[1].use = frame == 1;
            items.beforeStep(racers, inputs, 2, true);
            for (auto& r : racers) r.position += r.velocity;
            items.afterStep(racers, 2, floorQuery);
            for (const auto& a : items.actors()) {
                if (!a.active || a.kind != ItemKind::BlueShell) continue;
                if (a.explosion) { explosionFrame = frame; break; }
                assert(a.guidance.phase < 6);
                ++phases[a.guidance.phase];
                DriverSignals signals{};
                driverItemSignals(signals, items, 0);
                const bool expectedLook = a.guidance.phase >= 3 ||
                    (a.guidance.phase == 2 && a.guidance.ticks > 40);
                assert(signals.hasLookTarget == expectedLook);
                if (signals.hasLookTarget) {
                    Vec3 expected = a.visualPosition() + Vec3{0,50,0};
                    if (a.guidance.phase >= 4) expected += a.velocity;
                    assert(length(signals.lookTarget - expected) < .001f);
                }
                if (a.guidance.phase == 3) {
                    const auto offset = a.guidance.orbitOffset;
                    assert(std::fabs(dot(offset, racers[0].smoothedUp)) < .001f);
                    left |= offset.x < -20;
                    right |= offset.x > 20;
                }
            }
            if (explosionFrame) break;
        }
        std::printf("speed=%.0f approach=%u orbit=%u hover=%u dive=%u blast=%u\n",
            speed, phases[2], phases[3], phases[4], phases[5], explosionFrame);
        assert(explosionFrame);
        assert(phases[3] == 42); // sum(1000 + 35*n) first exceeds 0x11f40
        assert(phases[4] == 26); // state ticks 0..25 before pending dive
        assert(phases[5] > 0);
        assert(left && right);
    }
}
