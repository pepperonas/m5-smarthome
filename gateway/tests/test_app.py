"""HTTP surface: authentication and the shape of a reply."""

import json

import pytest

from m5gw import app as m5app

TOKEN = "test-token-not-a-real-secret"


class FakePoller:
    """Stands in for the network. Returns one fixed, realistic snapshot."""

    def __init__(self, raw=None, fresh=None):
        self.raw = raw if raw is not None else {
            "lw": {"power": False, "brightness": 255},
            "tf": {"powered": True, "volume": 29, "muted": False,
                   "currentInput": "AUX"},
        }
        self.fresh = fresh if fresh is not None else set(self.raw)
        self.calls = 0

    def collect(self, force=False):
        self.calls += 1
        return dict(self.raw), set(self.fresh)


@pytest.fixture
def client():
    return m5app.create_app(token=TOKEN, poller=FakePoller()).test_client()


def test_dash_without_a_token_is_refused():
    c = m5app.create_app(token=TOKEN, poller=FakePoller()).test_client()
    assert c.get("/api/dash").status_code == 401


def test_a_wrong_token_is_refused(client):
    assert client.get("/api/dash", headers={"Authorization": "Bearer nope"}
                      ).status_code == 401


def test_all_three_token_channels_work(client):
    for kwargs in ({"headers": {"Authorization": f"Bearer {TOKEN}"}},
                   {"headers": {"X-Token": TOKEN}},
                   {"query_string": {"t": TOKEN}}):
        assert client.get("/api/dash", **kwargs).status_code == 200


def test_a_gateway_without_a_configured_token_refuses_everyone():
    """Fail closed. An empty secret must not mean 'no secret required'."""
    c = m5app.create_app(token="", poller=FakePoller()).test_client()
    assert c.get("/api/dash", query_string={"t": ""}).status_code == 401
    assert c.post("/api/act", json={"target": "lw", "action": "off"}
                  ).status_code == 401


def test_health_needs_no_token_and_leaks_nothing():
    c = m5app.create_app(token=TOKEN, poller=FakePoller()).test_client()
    r = c.get("/api/health")
    assert r.status_code == 200
    assert TOKEN not in r.get_data(as_text=True)


def test_writes_are_refused_without_a_token(client):
    assert client.post("/api/act", json={"target": "fog", "action": "on",
                                         "confirm": True}).status_code == 401


def test_dash_is_compact_json(client):
    body = client.get("/api/dash", query_string={"t": TOKEN}).get_data(as_text=True)
    assert ", " not in body and '": ' not in body   # no wasted separators
    assert json.loads(body)["tf"]["est"] is True


def test_dash_is_served_from_cache_within_the_ttl():
    """One press must not fan out to eight backends every frame."""
    poller = FakePoller()
    c = m5app.create_app(token=TOKEN, poller=poller).test_client()
    for _ in range(5):
        c.get("/api/dash", query_string={"t": TOKEN})
    assert poller.calls == 1


def test_force_bypasses_the_cache():
    poller = FakePoller()
    c = m5app.create_app(token=TOKEN, poller=poller).test_client()
    c.get("/api/dash", query_string={"t": TOKEN})
    c.get("/api/dash", query_string={"t": TOKEN, "force": "1"})
    assert poller.calls == 2


def test_a_bad_action_answers_with_its_own_status(client):
    r = client.post("/api/act", query_string={"t": TOKEN},
                    json={"target": "fog", "action": "on"})
    assert r.status_code == 409                     # the fog interlock
    assert "confirm" in r.get_json()["error"]


def test_an_unknown_target_is_404(client):
    r = client.post("/api/act", query_string={"t": TOKEN},
                    json={"target": "toaster", "action": "on"})
    assert r.status_code == 404


def test_a_failed_backend_reports_502_and_says_which(client):
    # Nothing is listening on the configured loopback ports during tests,
    # so this exercises the real failure path.
    r = client.post("/api/act", query_string={"t": TOKEN},
                    json={"target": "lw", "action": "off"})
    assert r.status_code == 502
    assert r.get_json()["ok"] is False


def test_the_reply_tells_the_remote_what_it_may_assume(client):
    r = client.post("/api/act", query_string={"t": TOKEN},
                    json={"target": "lw", "action": "off"})
    # Even on failure the optimistic hint is present, so the remote knows
    # exactly which assumption to roll back.
    assert r.get_json()["applied"] == {"lw": {"on": False}}
