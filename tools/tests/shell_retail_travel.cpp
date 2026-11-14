#include "mkw/race_items.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

// Green and Red straight travel and Red lock-on rules against retail StaticR (USA):
// launch 0x807a6718, travel 0x807a6a94, Red states 0x8079fee0..0x807a06d4, target
// search 0x807a514c / 0x807a4dbc. Build with -DMKW_ITEMS_HOST_TEST (see item_shell_expiry).
using namespace mkw;

static ItemSurface flatGround(Vec3, Vec3 p, float radius) {
    ItemSurface s{};
    if (p.y < radius) { s.correction.y = radius - p.y; s.floorNormal = {0, 1, 0}; s.collided = true; }
    return s;
}
static ItemSurface noGround(Vec3, Vec3, float) { return {}; }

struct Race {
    RaceItems items;
    ItemRacer racers[3]{};
    unsigned count;
    ItemWorldQuery query;
    explicit Race(unsigned n, ItemWorldQuery q = flatGround, bool battle = false) : count(n), query(q) {
        items.reset({}, n);
        if (battle) items.loadOdds(nullptr, 0, 0, true);
        for (unsigned i = 0; i < n; ++i) {
            racers[i].grounded = true; racers[i].forward = {0, 0, 1}; racers[i].up = {0, 1, 0};
            racers[i].progress = float(n - i); // racer 0 leads
        }
    }
    void step(bool use = false, bool backwards = false) {
        ItemInput inputs[3]{};
        inputs[0].use = use; inputs[0].backwards = backwards;
        items.beforeStep(racers, inputs, count, true);
        items.afterStep(racers, count, query);
    }
    const ItemActor* throwItem(ItemKind kind, bool backwards) {
        assert(items.give(0, kind));
        step(true, backwards);  // equip
        step(false, backwards); // release
        for (unsigned wait = 0; wait < 12; ++wait) {
            for (const auto& a : items.actors()) if (a.active && a.kind == kind) return &a;
            step(false, backwards);
        }
        return nullptr;
    }
};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // A backward Red stays in state 1: it never looks for a target, even one in its path.
    {
        Race race(2);
        race.racers[1].position = {0, 0, -2500};
        const ItemActor* red = race.throwItem(ItemKind::RedShell, true);
        assert(red && red->guidance.phase == 1);
        for (unsigned f = 0; f < 60 && red->active; ++f) {
            race.step();
            assert(red->target < 0 && red->guidance.phase == 1);
        }
        assert(red->position.z < -200);
        std::printf("backward Red: straight, no lock: PASS\n");
    }
    // A forward Red with no item route (state 2) locks onto a racer ahead.
    {
        Race race(2);
        race.racers[1].position = {0, 0, 2500};
        const ItemActor* red = race.throwItem(ItemKind::RedShell, false);
        assert(red && red->guidance.phase == 2);
        for (unsigned f = 0; f < 30 && red->target < 0; ++f) race.step();
        assert(red->target == 1);
        std::printf("forward Red (no route): locks ahead: PASS\n");
    }
    // Finished racers stay eligible.
    {
        Race race(2);
        race.racers[1].position = {0, 0, 2500}; race.racers[1].finished = true;
        const ItemActor* red = race.throwItem(ItemKind::RedShell, false);
        for (unsigned f = 0; f < 30 && red->target < 0; ++f) race.step();
        assert(red->target == 1);
        for (unsigned f = 0; f < 5; ++f) race.step();
        assert(red->active && red->target == 1);
        std::printf("finished racer: locked and kept: PASS\n");
    }
    // Losing the lock (0x807a5460 -> 0x807a68cc): straight on along the current velocity,
    // not back along the launch heading.
    {
        Race race(2);
        race.racers[1].position = {1800, 0, 2500};
        const ItemActor* red = race.throwItem(ItemKind::RedShell, false);
        for (unsigned f = 0; f < 30 && red->target < 0; ++f) race.step();
        assert(red->target == 1);
        for (unsigned f = 0; f < 10; ++f) race.step();
        race.racers[1].shellTargetUnavailable = true;
        race.step();
        assert(red->active && red->target < 0);
        const Vec3 v = normalized(Vec3{red->velocity.x, 0, red->velocity.z}, {0, 0, 1});
        race.step();
        assert(red->guidance.phase == 6 && red->flightDirection.x > .2f);
        assert(dot(normalized(Vec3{red->flightDirection.x, 0, red->flightDirection.z}, {0, 0, 1}), v) > .99f);
        std::printf("lost lock: keeps its homing heading: PASS\n");
    }
    // An unranked (straight) Red can lock onto its own thrower once the thrower is ahead.
    {
        Race race(1);
        const ItemActor* red = race.throwItem(ItemKind::RedShell, false);
        assert(red && red->guidance.phase == 2);
        for (unsigned f = 0; f < 20; ++f) race.step();
        assert(red->target < 0);
        race.racers[0].position = red->position + Vec3{0, 0, 1500};
        for (unsigned f = 0; f < 8 && red->target < 0; ++f) race.step();
        assert(red->target == 0);
        std::printf("straight Red: thrower eligible: PASS\n");
    }
    // Backward launch speed: 30 minus the smaller of |velocity| and KartMove speed.
    {
        Race race(1, noGround);
        race.racers[0].position = {0, 50000, 0};
        race.racers[0].velocity = {0, 30, 80}; race.racers[0].haveVelocity = true; race.racers[0].speed = 60;
        const ItemActor* green = race.throwItem(ItemKind::GreenShell, true);
        assert(green);
        // Each update after the launch converges .01 toward 105 from 30 - 60.
        float launched = green->travelSpeed;
        for (unsigned n = 0; n < green->age; ++n) launched = (launched - .01f * 105.f) / .99f;
        assert(std::fabs(launched - -30.f) < .5f);
        assert(std::fabs(green->flightDirection.y) < 1e-4f && green->flightDirection.z < 0);
        std::printf("backward Green launch speed %.2f: PASS\n", launched);
    }
    // In the air the fall speed is capped at 100 once the airborne flag is set.
    {
        Race race(1, noGround);
        race.racers[0].position = {0, 50000, 0};
        const ItemActor* green = race.throwItem(ItemKind::GreenShell, false);
        for (unsigned f = 0; f < 80; ++f) race.step();
        assert(green->active && std::fabs(green->velocity.y - -100.f) < .01f);
        std::printf("airborne Green: fall capped at 100: PASS\n");
    }
    // Any item below y = 0 is removed.
    {
        Race race(1, noGround);
        const ItemActor* green = race.throwItem(ItemKind::GreenShell, false);
        for (unsigned f = 0; f < 40 && green->active; ++f) race.step();
        assert(!green->active);
        std::printf("Green below y=0: removed: PASS\n");
    }
    // Battle bounce counter: 3 backward / 7 forward (limits 4 / 8), races 4 / 9 (5 / 10).
    for (bool battle : {false, true}) for (bool backwards : {false, true}) {
        Race race(1, flatGround, battle);
        const ItemActor* green = race.throwItem(ItemKind::GreenShell, backwards);
        assert(green);
        const unsigned expected = battle ? (backwards ? 4 : 8) : (backwards ? 5 : 10);
        assert(green->bounceLimit == expected);
    }
    std::printf("Green bounce limits (race 5/10, Battle 4/8): PASS\n");
    // A shell thrown on a half-pipe starts its 30-frame timer: no speed convergence.
    {
        Race race(1, noGround);
        race.racers[0].position = {0, 50000, 0}; race.racers[0].halfPipe = true;
        const ItemActor* green = race.throwItem(ItemKind::GreenShell, false);
        const float speed = green->travelSpeed;
        race.step();
        assert(green->guidance.slopeTicks == 30 && green->travelSpeed == speed);
        std::printf("half-pipe throw: timer 30, speed held: PASS\n");
    }
}
