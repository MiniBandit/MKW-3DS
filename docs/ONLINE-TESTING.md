# Playing online

Online races are **peer to peer**: the consoles send their karts straight to
each other. There is no MKW 3DS server, so nothing to pay for or keep running.
Up to 12 players, VS races on the course the host chooses.

## What you need

- The same MKW 3DS build as everyone else (the official release). Consoles
  with a different build are turned away.
- Wi-Fi with internet.

## Race

1. Choose **Online** on the main menu, then your driver, vehicle and a course
   (the host's course is the one raced).
2. Choose **A Host a race** or **Y Join a race**.
3. Each console shows **YOUR CODE**, e.g. `7K2D-QX9M-4PA`.
   - **Host:** send your code to the other players (Discord, a message...).
   - **Joining:** type the host's code.
4. When everyone is in the lobby, the host presses **A** to start. A handshake
   appears and the race loads.

If a player can't get in after about ten seconds, their screen says so: the
host presses **X** and types **that player's** code. Some home routers only let
a console in once it has "called" it first.

After a race, go back into **Online** to race again: joining players don't
need to type the code a second time.

**B** leaves the lobby and races offline.

## Good to know

- **A code contains your internet address** (like any online game, the
  consoles have to know where to send packets). Share codes with people you're
  racing, not in big public channels.
- Most home internet works. Some mobile hotspots and some providers that share
  one address between many homes can't link directly; the screen warns when
  your router changes ports. A player who can't reach another player directly
  gets that player's kart through the host instead.
- If a console goes quiet for 5 seconds, its kart is driven by a CPU until it
  comes back.

## Same Wi-Fi, no internet (testing)

A file named `lan.txt` in `sd:/3ds/mkw-3ds/` skips the codes: `host` on one
console and `join` plus the address the host's screen shows (e.g.
`join 192.168.1.20`) on the others. Then choose VS as usual. Delete `lan.txt`
to go back to normal.

## Please report

Which console, what the screen said, whether the other karts moved smoothly,
and anything odd.
