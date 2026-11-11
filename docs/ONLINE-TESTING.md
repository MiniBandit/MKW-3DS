# Testing online play

An early test: two players, one race at a time, VS on a course the host chooses.

## What you need

- MKW 3DS built from the same version as everyone else, on a console with Wi-Fi.
- The server address, unless your build has it built in.

## Set up

If your build has no server built in, put a text file named `online.txt` in
`sd:/3ds/mkw-3ds/` containing the server address on one line, e.g.:

```
203.0.113.7
```

To skip typing the room code each time, add the port and the code after it:
`203.0.113.7 41750 MARIO7`.

## Race

1. Both players agree on a **room code** (letters and numbers, up to 8), e.g. `MARIO7`.
2. Choose **Online** on the main menu, then your driver, vehicle and a course.
   When you start, the game asks for the room code: both type the same one.
3. A terminal shows the connection steps. When you're connected, a handshake
   appears and the race loads. The first player in the room is the host and
   their course is used.

Press **B** on that screen to give up and race offline instead.

## Other set-ups (testing)

- **Same Wi-Fi, no server:** a file named `lan.txt` in `sd:/3ds/mkw-3ds/` with
  `host` on one console and `join` plus the address the host's screen shows
  (e.g. `join 192.168.1.20`) on the other. Then choose VS as usual.
- Remove `online.txt` and `lan.txt` to go back to the built-in settings.

## Please report

Which console, what the terminal said, whether the other kart moved smoothly,
and anything odd. Known limits of this test: two players only, no CPU racers,
items aren't shared yet, and the two consoles may disagree about positions.
