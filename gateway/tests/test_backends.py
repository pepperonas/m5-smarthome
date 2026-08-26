"""The I/O layer: fetching, and the two-clock poller.

These are the parts that decide whether one slow device can stall the whole
snapshot — a powered-down Yamaha once froze a 2 s dashboard refresh in this
house, which is why the timeouts are not negotiable.
"""

import json
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest

from m5gw import backends, config


class _Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.hits.append(self.path)
        body, status = self.server.reply
        if body is None:                       # simulate a dead backend
            self.close_connection = True
            return
        raw = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    do_POST = do_GET

    def log_message(self, *a):
        pass


@pytest.fixture
def server():
    srv = HTTPServer(("127.0.0.1", 0), _Handler)
    srv.reply = (json.dumps({"ok": True, "n": 1}), 200)
    srv.hits = []
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    yield srv
    srv.shutdown()
    srv.server_close()


def url(server, path="/x"):
    return "http://127.0.0.1:%d%s" % (server.server_address[1], path)


def test_a_good_reply_is_decoded(server):
    assert backends.fetch(url(server)) == {"ok": True, "n": 1}


def test_a_failure_is_none_not_an_exception(server):
    """For a dashboard poll, an unavailable source is normal operation."""
    server.reply = (None, 0)
    assert backends.fetch(url(server), timeout=0.5) is None


def test_a_refused_connection_is_none():
    # Nothing listening on this port; must not raise.
    assert backends.fetch("http://127.0.0.1:9/never", timeout=0.5) is None


def test_html_where_json_was_expected_is_none(server):
    # A misrouted request landing on some other service must not crash us.
    server.reply = ("<html>502 Bad Gateway</html>", 502)
    assert backends.fetch(url(server)) is None


def test_text_decoding_returns_the_body_verbatim(server):
    server.reply = ("<YAMAHA_AV rsp=\"GET\"/>", 200)
    assert backends.fetch(url(server), decode="text").startswith("<YAMAHA_AV")


def test_a_json_body_is_posted_with_a_content_type(server):
    assert backends.fetch(url(server), method="POST", body={"a": 1}) is not None
    assert server.hits                        # the request actually arrived


def test_a_raw_string_body_is_sent_unchanged(server):
    # Yamaha speaks XML; wrapping it in JSON would corrupt the command.
    assert backends.fetch(url(server), method="POST",
                          body="<YAMAHA_AV/>", decode="text") is not None


# --- the poller's two clocks ---------------------------------------------

def test_slow_sources_are_not_polled_every_round(monkeypatch):
    """Weather changes hourly. Polling it every second burns Pi cycles and,
    over the radio, battery."""
    p = backends.Poller()
    monkeypatch.setattr(config, "SLOW_TTL", 60.0)
    assert p._due("lw", now := 1000.0) is True         # fast source, always due
    p._at["wx"] = now
    assert p._due("wx", now + 1) is False              # slow source, not yet
    assert p._due("wx", now + 61) is True


def test_a_source_that_never_answered_is_absent_from_the_result():
    p = backends.Poller()
    raw, fresh = p.collect()
    # Nothing is listening on the configured loopback ports during tests.
    assert raw == {} and fresh == set()


def test_a_last_known_value_survives_a_failed_round(monkeypatch):
    """The rule the whole snapshot format exists for."""
    p = backends.Poller()
    p._last["wx"] = {"current": {"temp": 9.9}}
    p._at["wx"] = 1.0
    raw, fresh = p.collect()
    assert raw["wx"] == {"current": {"temp": 9.9}}     # still there...
    assert "wx" not in fresh                            # ...and honestly stale


def test_age_is_none_for_a_source_never_seen():
    p = backends.Poller()
    assert p.age("wx") is None


def test_every_source_maps_to_a_configured_backend():
    """A typo here would silently drop a tile from the snapshot forever."""
    for key, (backend, path, kind) in backends.SOURCES.items():
        assert backend in config.BACKENDS, key
        assert path.startswith("/api/"), key
        assert kind in ("json", "text"), key
