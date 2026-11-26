# Playing online

Online races are **peer to peer**: the consoles send their karts straight to
each other. There is no MKW 3DS server, so nothing to pay for or keep running.
Up to 12 players, VS races on the course the host chooses.

## What you need

- The same MKW 3DS version as everyone else (the official release). Consoles
  with a different version are turned away; a race on the same Wi-Fi with
  another version says so on the number pad.
- Wi-Fi with internet.

## Race

1. Choose **Online** on the main menu, then **Host a Race** or **Join a Race**.
2. Choose your driver, vehicle and drift. The host then chooses the course.
3. **Joining on the host's Wi-Fi:** no code. The host's race shows up on the
   number pad in green by itself; press **Y** (or **OK** with nothing typed).
4. **Joining over the internet:** type the host's 16-digit code on the number
   pad (B deletes a digit) and choose **OK**.
5. The **lobby** shows everyone who is in, with their driver and license name.
   - **Host:** your code is at the top of the lower screen, e.g.
     `1396-3797-6834-8383`: send it to players on other Wi-Fi (Discord, a
     message...), then choose **Start Race** when everyone is in.
   - **Joining:** wait for the host to start.

Most home routers let players in with the host's code alone: the host's
console asks its router to (UPnP, as game consoles do), and the host's lobby
then says "Players on any Wi-Fi join with your code." When it says "Player
stuck? Add a Player, type their code." instead, a player who can't get in
after about ten seconds is told so in their lobby: the host chooses
**Add a Player** and types **that player's** code (shown in their lobby).

After a race, **Next** takes the host to the course choice and everyone else
back to the lobby: nobody types a code again. If the host's console goes off
during a race, the others finish it and their lobby says the host has gone.

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
