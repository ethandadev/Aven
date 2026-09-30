# Online multiplayer

Friends on different networks (at home, at school, on a phone hotspot) can play a Rynax game
together through a **relay**: a small server that the host and the players all connect *out* to.
Nobody has to open ports on their router or know an IP address. The host gets a **room code**
like `KX7P2M`; the others type it in.

Everything else works as it does on a local network ([Multiplayer](advanced.md#multiplayer-on-the-local-network)):
`send()`, `on_receive`, NetworkSync, `spawn_networked()`, `on_player_joined`... The host's game is
in charge; the relay only passes messages along.

## In a game

```easyscript
def on_key_pressed(key):
    if key == "h":
        host_online()                  # asks the relay for a room
    if key == "j":
        join_online(game.typed_code)   # the code the host sees

def on_room_ready(code):               # hosting: the relay gave us a code to share
    say("Room code: " + code)

def on_connected():                    # joined (the host is connected straight away)
    spawn_networked("prefabs/player.prefab", 0, 0)
```

- `host_online(relay="")` starts hosting. `room_code()` is the code once it's ready ("" before),
  and `on_room_ready(code)` runs then.
- `join_online(code, relay="")` joins. Capitals and dashes don't matter (`kx7-p2m` works).
  `on_connected()` runs once the host has let the player in; if the code is wrong, the Console
  says why ("There's no game with the code ...").
- `leave_game()`, `is_online()`, `is_host()`, `player_id()`, `players()`: as on a local network.
- Only the same game can join: a code for another game is refused.

Which relay: **Project Settings > Game > Online relay** (for example `relay.example.com` or
`203.0.113.7:4243`), or pass `relay="..."` to either function.

## Running a relay

`rynax-relay` comes with the Linux download of Rynax (and builds with the rest of Rynax from source on
any system). It needs no screen and very little: the smallest cloud server (1 CPU, 512 MB) handles
hundreds of games. It listens on TCP port **4243**.

1. Get a Linux server with a public address (any cloud provider, or a computer at home with that
   port forwarded).
2. Copy `rynax-relay` from the Linux download onto it and start it:
   ```sh
   ./rynax-relay                  # --port 4243 --max-rooms 1000 --max-players 16 --rate 512
   ```
3. Allow TCP port 4243 in the server's firewall (for example `sudo ufw allow 4243/tcp`, or the
   provider's security group).
4. Put its address in Project Settings > Game > Online relay, and export the game.

To keep it running after you log out and after restarts, a systemd service
(`/etc/systemd/system/rynax-relay.service`):

```ini
[Unit]
Description=Rynax multiplayer relay
After=network-online.target

[Service]
ExecStart=/opt/rynax/rynax-relay
Restart=always
DynamicUser=yes

[Install]
WantedBy=multi-user.target
```

then `sudo systemctl enable --now rynax-relay`. Or in a container:

```dockerfile
FROM debian:stable-slim
COPY rynax-relay /usr/local/bin/
EXPOSE 4243
USER nobody
ENTRYPOINT ["rynax-relay"]
```

It prints a line a minute when the number of games changes, and `rynax-relay: listening on port
4243` at the start.

## Limits, and what the relay can see

- **Rooms and players**: 1000 games at once and 16 players each by default (`--max-rooms`,
  `--max-players`). A room ends when its host leaves; the players are told.
- **Fair use**: each connection may send about 512 KB a second on average (`--rate`, in KB), and
  one that stops reading is let go rather than filling the server's memory.
- **Guessing codes**: codes are 6 characters from 32 (about a billion), and an address that gets
  20 wrong in a minute has to wait.
- **Idle connections** that don't say what they want within 10 seconds are closed.
- **No encryption**: messages travel as they are (like a local network game). The relay and
  anyone between can read them. Don't send passwords or anything private through `send()`.
- **No accounts**: a room code is the only key. Anyone with it can join, while the room lasts.
- The relay works for games made with the same version family (it checks a protocol version and
  says so if they don't match).

## How it works

The host's game connects to the relay and asks for a room; the relay answers with the code.
A player connects and asks to join that code, for the same game; the relay tells the host a new
player has arrived, and from then on passes each message between them, in order, over the same
TCP connections. The host sees each player as if they had connected directly, so the rules of
[local multiplayer](advanced.md#multiplayer-on-the-local-network) (who owns what, who may move
what) are the same. The protocol is in `engine/src/rynax/runtime/relay.h`.
