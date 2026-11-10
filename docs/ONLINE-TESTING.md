# Testing online play

This is an early test: two players, one race at a time, VS mode.

## What you need

- MKW 3DS built from the same version as everyone else (the test build you were
  given), on a console with Wi-Fi.
- The server address, from whoever runs the server.

## Set up

Put a text file named `lan.txt` in `sd:/3ds/mkw-3ds/` containing one line:

```
online SERVER_ADDRESS
```

To skip typing the room code each time, add it after the address, e.g.
`online SERVER_ADDRESS MARIO7`. Both players use the same code.

## Race

1. Both players agree on a **room code** (letters and numbers, up to 8), e.g. `MARIO7`.
2. Choose **VS → Solo**, then your character, vehicle and a course. When you start,
   the game asks for the room code: both type the same one.
3. A terminal shows the connection steps. When you're connected, a handshake
   appears and the race loads. The first player in the room is the host and
   their course is used.

Press **B** on that screen to give up and race offline instead.

## Other set-ups

- **Same Wi-Fi, no server:** one console's `lan.txt` says `host`; the other's says
  `join` followed by the address the host's screen shows (e.g. `join 192.168.1.20`).
- **Remove `lan.txt`** to play offline as usual.

## Please report

Which console, what the terminal said, whether the other kart moved smoothly,
and anything odd. Known limits of this test: two players only, no CPU racers,
items aren't shared yet, and the two consoles may disagree about positions.
