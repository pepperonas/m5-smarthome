# Gateway operations

A Flask service that aggregates the house APIs for the remote. It runs
**alongside** the existing stack and changes nothing about it: new port, new
systemd unit, read-only access to the other services over loopback.

## Why Python and Flask

Because that is what this house already runs: hue, fog, disco, both climate
apps and the weather proxy are all Flask under systemd with a venv, and the
deploy pattern, the logs and the muscle memory are shared. The work here is
I/O-bound — ten concurrent HTTP fetches against loopback — not CPU-bound, so
a thread pool is the right tool and Go would buy nothing but a second toolchain.
It is ~600 lines and starts in well under a second.

## Install

```bash
cd gateway
./deploy.sh raspi5          # or whatever your Pi is called in ~/.ssh/config
```

The script is idempotent and does, in order:

1. runs the test suite locally and stops if it is red;
2. rsyncs `m5gw/`, `tests/` and `requirements.txt` to
   `/home/pi/apps/m5-gateway/` (never `.env`);
3. creates a venv with `--system-site-packages` and installs Flask;
4. **generates a token on first run only** —
   `openssl rand -hex 24` into `.env`, mode 600 — and leaves an existing one
   alone;
5. installs and enables `m5-gateway.service`;
6. curls `/api/health`.

## Configuration

`/home/pi/apps/m5-gateway/.env`, mode 600, root-readable only by the service
user. **It is never committed** — the repository carries
`m5-gateway.env.example` and nothing else.

| variable | default | meaning |
|---|---|---|
| `M5GW_TOKEN` | *(none)* | shared secret. **No default: the service refuses to start without it.** |
| `M5GW_PORT` | `5010` | listening port |
| `M5GW_BIND` | `0.0.0.0` | the remote is on the LAN, so this binds LAN-wide |
| `M5GW_FETCH_TIMEOUT` | `1.5` | per-backend ceiling when polling |
| `M5GW_ACT_TIMEOUT` | `3.0` | ceiling for a write action |
| `M5GW_CACHE_TTL` | `1.0` | how long a snapshot is reused |
| `M5GW_SLOW_TTL` | `60.0` | refresh interval for weather, climate and Pi health |
| `M5GW_HUE` … `M5GW_PI` | loopback URLs | override a backend address |

Read the token back when setting up a device:

```bash
ssh raspi5 'grep M5GW_TOKEN /home/pi/apps/m5-gateway/.env'
```

## Checking it

```bash
ssh raspi5 'systemctl status m5-gateway'
ssh raspi5 'journalctl -u m5-gateway -f'
ssh raspi5 'curl -s localhost:5010/api/health'

T=$(ssh raspi5 'grep -oP "(?<=M5GW_TOKEN=).*" /home/pi/apps/m5-gateway/.env')
ssh raspi5 "curl -s -H 'Authorization: Bearer $T' localhost:5010/api/dash"
```

A useful one-liner for size and latency, which is how the numbers in the docs
were obtained:

```bash
curl -s -o /dev/null -w '%{size_download} B  %{time_total} s\n' \
     -H "Authorization: Bearer $T" 'localhost:5010/api/dash?force=1'
```

## Ports

The gateway takes **5010**, which sits in the same range as the rest of the
house and was free. Before adding anything else, check:

```bash
ssh raspi5 'ss -tlnp | grep -oE "127.0.0.1:[0-9]+|0.0.0.0:[0-9]+" | sort -u'
```

Backends it expects, all on loopback:

| service | port |
|---|---|
| hue | 5000 |
| yamaha (XML proxy) | 5001 |
| teufel / powerhifi | 5002 |
| fog | 5003 |
| lichtwerk (strip) | 5006 |
| disco | 5007 |
| indoor climate | 5008 |
| garden climate | 5009 |
| weather | 5011 |
| raspi-monitor | 4999 |

Three of those (5008, 5009, 5011) are bound to `127.0.0.1` and are reachable
*only* through this gateway.

## Testing

```bash
cd gateway && python3 -m pytest -q          # 58 tests
python3 ../tools/mutate.py gateway          # 6 mutations, all must be caught
```

The mutation harness removes the fog interlock, makes toggles guess, stops
marking stale sources, lets entertainment zones leak into the room list, makes
an empty token mean "no auth", and bypasses the cache. Each must turn the
suite red. It restores every file in a `finally` — an earlier ad-hoc version
crashed mid-run and left three mutated files in the tree, which is exactly the
failure it now guards against.

## Updating

```bash
cd gateway && ./deploy.sh raspi5
```

The existing `.env` survives; the token does not change.

## Rollback

```bash
ssh raspi5 'sudo systemctl disable --now m5-gateway'
```

Nothing else is affected. The gateway only ever *reads* from the other
services and *writes* through their existing public APIs — the same calls the
web dashboard already makes. Removing it leaves the house exactly as it was.

To remove it completely:

```bash
ssh raspi5 'sudo systemctl disable --now m5-gateway && \
            sudo rm /etc/systemd/system/m5-gateway.service && \
            sudo systemctl daemon-reload && \
            rm -rf /home/pi/apps/m5-gateway'
```

## Security notes

- The service runs as `pi` under a systemd sandbox: `NoNewPrivileges`,
  `ProtectSystem=strict`, `ProtectHome=read-only`, `PrivateTmp`, with only its
  own directory writable.
- There is **no generic pass-through**. Only the named actions in
  [API.md](API.md#actions) exist; a path the remote controls would be a hole
  into six unauthenticated loopback services.
- Fog ignition requires `confirm: true` at the gateway, independently of
  whatever the remote's UI does. Two interlocks, deliberately.
- The token lives in `.env` on the Pi and in NVS on the device. It is in
  neither the repository nor the firmware binary.
