# Multiplayer plan

Status: planning only. No network code exists in the game yet.

Based on reading the public sources on 2026-11-04: Retro Rewind's
[wfc-server](https://github.com/Retro-Rewind-Team/wfc-server) (AGPL-3.0),
[wfc-patcher-wii](https://github.com/Retro-Rewind-Team/wfc-patcher-wii) and
[rr-pulsar](https://github.com/Retro-Rewind-Team/rr-pulsar) (GPL-3.0). Nothing
here has been tried against their server or on a console.

## The main decision

**Recommendation: 3DS players race each other on Retro Rewind's servers first,
and racing Wii players comes later, if at all.**

| | 3DS-only rooms | Racing Wii Retro Rewind players |
|---|---|---|
| Courses | The 42 the game already has | Every course in Retro Rewind's pack, on a console that has about 2.5 MB of graphics memory spare in a race on Old 3DS |
| Packets | Ours, modelled on the Wii's | Must pass the checks in every Wii player's patched game |
| Updates | On our schedule | Every Retro Rewind update can break us |
| Their server | A login path for 3DS clients and a separate room region | The same, plus changes to their game client |

Using the Wii's packet layouts in 3DS-only rooms keeps the door open without
paying for it now.

## What their server gives us

These parts can be used as they are, once a 3DS client is allowed to log in:

| Service | Folder | Job |
|---|---|---|
| NAS | `nas/` | First login, issues the auth token |
| GPCM, GPSP | `gpcm/`, `gpsp/` | Profiles, friend codes, friend presence |
| QR2, server browser | `qr2/`, `serverbrowser/` | Finding and joining rooms |
| NATNEG | `natneg/` | Connecting two players directly through their routers |
| SAKE, gamestats | `sake/`, `gamestats/` | Stored data and statistics |

Races are peer to peer: once matched, players send UDP directly to each other,
so the server doesn't carry race traffic.

## Blockers that need Retro Rewind

1. **Login.** `gpcm/login.go` requires `wl:ver` and `wl:sig`. The signature is
   checked through a chain ending in a Wii's own device certificate, which a
   3DS can't produce. `allowDefaultDolphinKeys` is not a fit: it's for Dolphin,
   and every player would share one device ID.
2. **Client hash.** With `enableHashCheck` on, `mariokartwii` logins need a
   `pack_id`, version and `pack_hash` listed in the server's `hashes` table.
   Each game update needs its hash registered, or logins fail.
3. **Payload.** The server hands Wii clients PowerPC code to patch the game.
   A 3DS can't run it, so 3DS clients must be exempt.
4. **Bans and moderation.** Bans lean on the Wii device ID. A 3DS needs its own
   stable ID, and a modified console can fake one.
5. **Keeping players apart.** A separate `rk` region (rooms are grouped by it in
   `qr2/group.go`) stops Wii and 3DS players being matched by accident. The
   server reports a `wl:host` platform per client, which may help here.

A small new login path on their side, marked with a 3DS platform value, looks
like the cleanest answer to 1 to 4. That's their decision.

**Licences.** Their server is AGPL and their client GPL; this repository is MIT
for our own changes, and Epic's contributions have no declared licence. We
shouldn't copy their code into the game. Their README says authors can grant
exemptions, so ask if we need any.

## What has to be built in the game

**Network layer (new).** `soc:U` sockets, Wi-Fi handling and clean recovery from
sleep. Everything runs on its own thread with a fixed memory budget. Sockets need
their own buffer, which is significant on Old 3DS (see Risks).

**Login and identity.** NAS and GPCM login, a generated or derived profile, and
a friend code shown on the license.

**Rooms.** QR2 hosting and joining, NATNEG, and the host, join, vote and
disconnect flow. The menus currently mark Wi-Fi as unavailable, so these are new
pages in Epic's menu style.

**Race sync.** The Wii's packets, as declared in rr-pulsar's
`GameSource/MarioKartWii/RKNet`:

| Packet | Size (bytes) | Carries |
|---|---|---|
| Header | 0x10 | Section sizes and checksum |
| RH1 | 0x30 | Timer and race state |
| ROOM | 0x4 | Room messages |
| SELECT | 0x38 | Character, vehicle, course vote |
| RACEDATA | 0x40 per player | Position, speed, inputs, state flags |
| USER | 0xC0 | Mii and player data |
| ITEM | 0x8 | Held and trailing items |
| EVENT | 0xF8 | Item and hit events |

The Wii game sends kart state rather than running in lockstep, so remote players
don't have to match our physics exactly. A remote kart would be placed from those
packets and smoothed between them. Kinoko currently simulates every kart locally
for the race, so this is the largest piece of engine work: a remote mode for
`src/race_grid.cpp`, items and collisions.

**Packet checking.** Every incoming packet needs checking before use. Mario Kart
Wii's own online code had a remote code execution bug from a crafted packet
(CVE-2023-35856). `payload/mkwSecurity.cpp` shows which fields the Wii client
validates; ours has to be written fresh.

## Phases

Each phase ends with something testable, and none depends on the Wii client.

0. **Agree with Retro Rewind.** Send the questions below and settle the login
   approach, using their test server rather than production.
1. **Two 3DSes on one network.** UDP with a typed-in address and no server: two
   players race Luigi Circuit and see each other drive. This proves or disproves
   the remote kart approach cheaply, and it's the go/no-go point.
2. **Full race sync.** Twelve racers, items, hits, finish order, disconnects, on
   real consoles over bad Wi-Fi.
3. **Log in to their server.** Login, friend codes, presence.
4. **Friend rooms.** QR2 and NATNEG across the internet, course votes, results.
5. **Public rooms.** Worldwide 3DS matchmaking, moderation, a version check.
6. **Optional: racing Wii players.** Only if 3DS rooms go well, and only with
   Retro Rewind's agreement.

Phase 1 can start before phase 0 finishes, since it doesn't touch their service.

## Risks

- **Old 3DS may not manage it.** In a race it has about 2.5 MB LINEAR and 2 MB
  heap free in Azahar tests, and hardware speed is unmeasured. Plan on New 3DS
  first, then measure Old 3DS, possibly with fewer racers.
- **Emulators can't prove this.** Timing, Wi-Fi and NAT behaviour all need real
  consoles, starting at phase 1.
- **Real-time pressure.** Single-player can slow down; online can't. A console
  that falls behind has to catch up or drop out.
- **Router compatibility.** NATNEG fails on some networks, and 3DS Wi-Fi is
  older and slower than a PC's.
- **Cheating.** The client is open source and consoles are modified, so a hash
  check only blocks honest mistakes. Server-side moderation does the real work.
- **Load on their service.** It's run by volunteers, and a popular release could
  add a lot of players.

## Questions for Retro Rewind

1. Does your permission cover 3DS clients logging in to play.rwfc.net, and can
   we use your test server during development?
2. Would you add a login path for a 3DS client, given it can't produce a Wii
   signature or run the payload? If so, what should it send?
3. Can we have our own `pack_id` and `rk` region so 3DS rooms stay separate?
4. How should bans work for 3DS players, and who moderates them?
5. How do you want hashes registered for each of our updates?
6. Are you comfortable with the extra players, or do you want a limit or a slow
   rollout?
7. Would you ever want Wii and 3DS players racing together?
8. If we need any of your GPL or AGPL code, would you grant an exemption?
