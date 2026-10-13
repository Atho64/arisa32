#!/usr/bin/env python3
"""Minimal MCP streamable-HTTP client for the local ida-pro-mcp server.

Usage:
  mcp_client.py list                          # list tools
  mcp_client.py call <tool> [json-args]       # call a tool
  mcp_client.py raw <method> [json-params]    # raw JSON-RPC
"""
import json
import os
import sys
import tempfile
import urllib.request

BASE = os.environ.get("IDA_MCP_URL", "http://127.0.0.1:13337/mcp")
SESSION_FILE = os.path.join(tempfile.gettempdir(), "dc4_ida_mcp_session.txt")


def post(payload, session=None, notify=False):
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if session:
        headers["Mcp-Session-Id"] = session
    req = urllib.request.Request(BASE, json.dumps(payload).encode(), headers)
    with urllib.request.urlopen(req, timeout=120) as r:
        sid = r.headers.get("Mcp-Session-Id")
        body = r.read()
    if sid and session is None:
        open(SESSION_FILE, "w").write(sid)
    return body


def ensure_session():
    try:
        return open(SESSION_FILE).read().strip()
    except OSError:
        body = post({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                     "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                                "clientInfo": {"name": "zcode", "version": "1.0"}}})
        # Current ida-pro-mcp is stateless and does not issue a session ID.
        try:
            sid = open(SESSION_FILE).read().strip()
        except OSError:
            sid = None
        try:
            post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
        except Exception:
            pass
        return sid


def rpc(method, params=None, id_=1, session=None):
    payload = {"jsonrpc": "2.0", "id": id_, "method": method}
    if params is not None:
        payload["params"] = params
    body = post(payload, session)
    if not body.strip():
        return {}
    if body.lstrip().startswith(b"data:") or body.lstrip().startswith(b"event:"):
        events = [line[5:].strip() for line in body.splitlines() if line.startswith(b"data:")]
        return json.loads(events[-1]) if events else {}
    return json.loads(body)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    sid = ensure_session()
    if cmd == "list":
        resp = rpc("tools/list", {}, session=sid)
        for t in resp.get("result", {}).get("tools", []):
            print("%-40s %s" % (t["name"], (t.get("description") or "").splitlines()[0][:90]))
    elif cmd == "call":
        tool, args = sys.argv[2], json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
        resp = rpc("tools/call", {"name": tool, "arguments": args}, session=sid)
        out = resp.get("result", resp)
        for c in out.get("content", []):
            print(c.get("text", json.dumps(c)))
        if out.get("isError"):
            print("[ERROR]", file=sys.stderr)
    elif cmd == "raw":
        print(json.dumps(rpc(sys.argv[2], json.loads(sys.argv[3]) if len(sys.argv) > 3 else None, session=sid), indent=1))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
