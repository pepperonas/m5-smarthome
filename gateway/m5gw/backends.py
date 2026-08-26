"""The I/O half: fetch from many backends at once, never block on one.

Kept deliberately thin — all decisions live in the pure modules next door.
"""

import json
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

from . import config, yamaha_xml

#: source key -> (backend key, path, kind)
#: kind 'json' decodes, 'xml' returns text.
SOURCES = {
    "hue_groups": ("hue", "/api/groups", "json"),
    "lw":         ("lw", "/api/status", "json"),
    "tf":         ("tf", "/api/status", "json"),
    "fog":        ("fog", "/api/status", "json"),
    "tank":       ("fog", "/api/tank", "json"),
    "disco":      ("disco", "/api/status", "json"),
    "clima_in":   ("clima", "/api/current", "json"),
    "clima_out":  ("garten", "/api/current", "json"),
    "wx":         ("wx", "/api/weather", "json"),
    "pi":         ("pi", "/api/metrics", "json"),
}


def fetch(url, *, method="GET", body=None, headers=None, timeout=None,
          decode="json"):
    """One request. Returns decoded body, or None on any failure.

    Returning None rather than raising is intentional: for a dashboard poll
    'this source is unavailable' is normal operation, not an exception.
    """
    timeout = timeout or config.FETCH_TIMEOUT
    data = None
    hdrs = dict(headers or {})
    if body is not None:
        if isinstance(body, (bytes, str)):
            data = body.encode() if isinstance(body, str) else body
        else:
            data = json.dumps(body).encode()
            hdrs.setdefault("Content-Type", "application/json")
    req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
    except (urllib.error.URLError, OSError, ValueError):
        return None
    if decode == "raw":
        return raw
    text = raw.decode("utf-8", "replace")
    if decode == "text":
        return text
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def fetch_yamaha_status(timeout=None):
    """Yamaha has no JSON status; ask the receiver and parse its XML here."""
    xml = fetch(config.BACKENDS["yam"] +
                "/api/receiver/YamahaRemoteControl/ctrl",
                method="POST", body=yamaha_xml.status_request(),
                headers={"Content-Type": "text/xml; charset=UTF-8"},
                timeout=timeout, decode="text")
    if not xml:
        return None
    parsed = yamaha_xml.parse_status(xml)
    return parsed or None


class Poller:
    """Fetches all sources in parallel and remembers the last good value.

    Two caches with different clocks: everything refreshes at CACHE_TTL,
    except weather/climate/pi which move slowly and would only burn radio
    time and Pi cycles if polled every second.
    """

    def __init__(self, workers=8):
        self._pool = ThreadPoolExecutor(max_workers=workers,
                                        thread_name_prefix="m5gw")
        self._last: dict = {}        # source -> value (last successful)
        self._at: dict = {}          # source -> monotonic time of that value

    def _due(self, key, now):
        ttl = config.SLOW_TTL if key in config.SLOW_SOURCES else 0.0
        return (now - self._at.get(key, -1e9)) >= ttl

    def collect(self, force=False):
        """Returns (raw, fresh_keys). raw carries last-known values for
        sources that failed *this* round; fresh_keys says which ones are
        actually current, so the caller can be honest about staleness."""
        now = time.monotonic()
        jobs = {}
        for key, (backend, path, kind) in SOURCES.items():
            if not force and not self._due(key, now):
                continue
            url = config.BACKENDS[backend] + path
            jobs[key] = self._pool.submit(
                fetch, url, decode="json" if kind == "json" else "text")
        if force or self._due("yam", now):
            jobs["yam"] = self._pool.submit(fetch_yamaha_status)

        fresh = set()
        for key, fut in jobs.items():
            try:
                val = fut.result(timeout=config.FETCH_TIMEOUT + 0.5)
            except Exception:
                val = None
            if val is not None:
                self._last[key] = val
                self._at[key] = now
                fresh.add(key)
        # Sources we did not poll this round are still current by definition.
        for key in self._last:
            if key not in jobs:
                fresh.add(key)
        return dict(self._last), fresh

    def age(self, key):
        at = self._at.get(key)
        return None if at is None else time.monotonic() - at
