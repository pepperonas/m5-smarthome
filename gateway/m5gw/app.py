"""Flask app: three endpoints, one shared secret, no generic pass-through."""

import hmac
import json
import time

from flask import Flask, Response, request

from . import actions, aggregate, backends, config


def _unauthorized(msg="unauthorized"):
    return Response(json.dumps({"error": msg}), status=401,
                    mimetype="application/json")


def create_app(token: str | None = None, poller: backends.Poller | None = None):
    app = Flask(__name__)
    app.config["TOKEN"] = token if token is not None else config.TOKEN
    poller = poller or backends.Poller()
    cache = {"at": 0.0, "body": None, "dash": None}

    def authorized() -> bool:
        want = app.config["TOKEN"]
        if not want:
            # A gateway that can ignite a fog machine does not run open.
            return False
        got = ""
        auth = request.headers.get("Authorization", "")
        if auth.startswith("Bearer "):
            got = auth[7:]
        elif request.headers.get("X-Token"):
            got = request.headers["X-Token"]
        elif request.args.get("t"):
            # Query token exists for curl and for the ESP32's simplest path.
            got = request.args["t"]
        return hmac.compare_digest(got, want)

    @app.get("/api/health")
    def health():
        """Unauthenticated on purpose: liveness only, discloses nothing."""
        return {"ok": True, "service": "m5-smarthome-gateway"}

    @app.get("/api/dash")
    def dash():
        if not authorized():
            return _unauthorized()
        now = time.monotonic()
        force = request.args.get("force") == "1"
        if not force and cache["body"] and (now - cache["at"]) < config.CACHE_TTL:
            body = cache["body"]
        else:
            raw, fresh = poller.collect(force=force)
            snapshot = aggregate.build_dash(raw, int(time.time()), fresh)
            cache["dash"] = snapshot
            # separators= drops the spaces json.dumps adds by default; on a
            # ~900 byte payload that is a real fraction of the radio time.
            body = json.dumps(snapshot, separators=(",", ":"),
                              ensure_ascii=False)
            cache["at"] = now
            cache["body"] = body
        return Response(body, mimetype="application/json; charset=utf-8")

    def _run(plans):
        results, ok = [], True
        for p in plans:
            url = config.BACKENDS[p.backend] + p.path
            res = backends.fetch(url, method=p.method,
                                 body=p.data if p.data is not None else p.json,
                                 headers=p.headers,
                                 timeout=config.ACT_TIMEOUT,
                                 decode="text" if p.data is not None else "json")
            good = res is not None
            ok = ok and good
            results.append({"target": p.backend, "ok": good})
        # The snapshot we hold is now a lie; drop it so the next poll is real.
        cache["at"] = 0.0
        return ok, results

    @app.post("/api/act")
    def act():
        if not authorized():
            return _unauthorized()
        body = request.get_json(silent=True) or {}
        target = str(body.get("target") or "")
        action = str(body.get("action") or "")
        params = body.get("params") if isinstance(body.get("params"), dict) else body
        try:
            if target == "macro":
                plans = actions.macro(action, cache["dash"])
            else:
                plans = [actions.plan(target, action, params, cache["dash"])]
        except actions.ActionError as e:
            return Response(json.dumps({"error": e.message}), status=e.status,
                            mimetype="application/json")

        ok, results = _run(plans)
        opt = {}
        for p in plans:
            for k, v in p.optimistic.items():
                opt.setdefault(k, {}).update(v)
        payload = {"ok": ok, "applied": opt}
        if not ok:
            payload["results"] = results
        return Response(json.dumps(payload, separators=(",", ":")),
                        status=200 if ok else 502,
                        mimetype="application/json")

    return app


def main():
    if not config.TOKEN:
        raise SystemExit(
            "M5GW_TOKEN is not set. Refusing to start without a shared secret "
            "— this gateway can switch a 220 V fog machine.")
    app = create_app()
    app.run(host=config.BIND, port=config.PORT, threaded=True)


if __name__ == "__main__":
    main()
