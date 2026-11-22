# Playing online

Online races are **peer to peer**: the consoles send their karts straight to
each other. There is no MKW 3DS server, so nothing to pay for or keep running.
Up to 12 players, VS races on the course the host chooses.

## What you need

- The same MKW 3DS build as everyone else (the official release). Consoles
  with a different build are turned away.
- Wi-Fi with internet.

## Race

1. Choose **Online** on the main menu, then **Host a Race** or **Join a Race**.
2. Choose your driver, vehicle and drift. The host then chooses the course.
3. **Joining:** type the host's 16-digit code on the number pad (B deletes a
   digit) and choose **OK**. **On the same Wi-Fi as the host**, no code is
   needed: the number pad shows the host's race in green; press **Y**.
4. The **lobby** shows everyone who is in, with their driver and license name,
   and your own code, e.g. `1396-3797-6834-8383`.
   - **Host:** send your code to the other players (Discord, a message...),
     then choose **Start Race** when everyone is in.
   - **Joining:** wait for the host to start.

If a player can't get in after about ten seconds, the bottom line of their
lobby says so: the host chooses **Add a Player** and types **that player's**
code (shown in their lobby). Some home routers only let a console in once it
has "called" it first.

After a race, **Next** takes the host to the course choice and everyone else
back to the lobby: nobody types a code again.

**B** in the lobby leaves it.

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
