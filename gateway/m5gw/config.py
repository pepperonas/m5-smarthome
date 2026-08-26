"""Gateway configuration. Everything overridable by environment."""

import os

#: Loopback addresses of the backends this gateway aggregates.
#: They all live on the same Pi; the gateway is the only thing that talks
#: to them on behalf of the remote.
BACKENDS = {
    "hue":   os.environ.get("M5GW_HUE",   "http://127.0.0.1:5000"),
    "yam":   os.environ.get("M5GW_YAM",   "http://127.0.0.1:5001"),
    "tf":    os.environ.get("M5GW_TF",    "http://127.0.0.1:5002"),
    "fog":   os.environ.get("M5GW_FOG",   "http://127.0.0.1:5003"),
    "lw":    os.environ.get("M5GW_LW",    "http://127.0.0.1:5006"),
    "disco": os.environ.get("M5GW_DISCO", "http://127.0.0.1:5007"),
    "clima": os.environ.get("M5GW_CLIMA", "http://127.0.0.1:5008"),
    "garten": os.environ.get("M5GW_GARTEN", "http://127.0.0.1:5009"),
    "wx":    os.environ.get("M5GW_WX",    "http://127.0.0.1:5011"),
    "pi":    os.environ.get("M5GW_PI",    "http://127.0.0.1:4999"),
}

#: Per-request timeout when polling a backend, in seconds.
#: Short on purpose: a slow source must degrade to "stale" on the remote,
#: never delay the whole snapshot. (The dashboard learned this the hard way
#: when a powered-down Yamaha froze its 2 s refresh loop.)
FETCH_TIMEOUT = float(os.environ.get("M5GW_FETCH_TIMEOUT", "1.5"))

#: Timeout for a write action — a little longer, these are user-initiated.
ACT_TIMEOUT = float(os.environ.get("M5GW_ACT_TIMEOUT", "3.0"))

#: How long a /api/dash snapshot may be served from cache.
CACHE_TTL = float(os.environ.get("M5GW_CACHE_TTL", "1.0"))

#: Slow-moving sources are refreshed at a lower rate than the rest.
SLOW_TTL = float(os.environ.get("M5GW_SLOW_TTL", "60.0"))
SLOW_SOURCES = ("wx", "clima_in", "clima_out", "pi")

PORT = int(os.environ.get("M5GW_PORT", "5010"))
BIND = os.environ.get("M5GW_BIND", "0.0.0.0")

#: Shared secret. No default on purpose — the service refuses to start
#: without one rather than come up wide open.
TOKEN = os.environ.get("M5GW_TOKEN", "")
