"""Tests for the Nova browser client gateway. Run: python3 -m unittest -v test_nova_web_gateway"""

import asyncio
import unittest

import nova_web_gateway as g

TOKEN = "a" * 64
OTHER = "b" * 64


class PolicyTests(unittest.TestCase):
    def test_lan_and_tailscale_allowed(self):
        nets = g.networks_for("lan")
        for ip in ("127.0.0.1", "192.168.10.10", "10.1.2.3", "172.20.0.5", "100.100.10.1", "169.254.3.4",
                   "::1", "fe80::1%eth0", "fd7a:115c:a1e0::1", "::ffff:192.168.1.4"):
            self.assertTrue(g.address_allowed(ip, nets), ip)

    def test_public_refused(self):
        nets = g.networks_for("lan")
        for ip in ("8.8.8.8", "100.128.0.1", "172.32.0.1", "2001:4860::8888", "::ffff:1.1.1.1", "fe80:0:0:1::1", "garbage", ""):
            self.assertFalse(g.address_allowed(ip, nets), ip)

    def test_wan_is_capped_at_lan_and_pc_is_loopback_only(self):
        self.assertFalse(g.address_allowed("8.8.8.8", g.networks_for("wan")))
        self.assertTrue(g.address_allowed("192.168.1.2", g.networks_for("wan")))
        pc = g.networks_for("pc")
        self.assertTrue(g.address_allowed("127.0.0.1", pc))
        self.assertTrue(g.address_allowed("::1", pc))
        self.assertFalse(g.address_allowed("192.168.1.2", pc))

    def test_api_allowlist(self):
        self.assertTrue(g.api_allowed("GET", "/api/hosts"))
        self.assertTrue(g.api_allowed("HEAD", "/api/apps"))
        self.assertTrue(g.api_allowed("GET", "/api/host/stream"))
        self.assertTrue(g.api_allowed("POST", "/api/host/cancel"))
        for method, path in (("POST", "/api/host"), ("DELETE", "/api/host"), ("PATCH", "/api/host"), ("POST", "/api/pair"),
                             ("POST", "/api/login"), ("POST", "/api/user"), ("GET", "/api/users"), ("POST", "/api/role"),
                             ("GET", "/api/hosts/../users")):
            self.assertFalse(g.api_allowed(method, path), (method, path))

    def test_host_of(self):
        self.assertEqual(g.host_of("192.168.10.10:47995"), "192.168.10.10")
        self.assertEqual(g.host_of("atom.tail1234.ts.net:47995"), "atom.tail1234.ts.net")
        self.assertEqual(g.host_of("[fd7a::1]:47995"), "[fd7a::1]")
        self.assertEqual(g.host_of("localhost"), "localhost")
        for bad in (None, "", "evil.com/x", "a b", "[fe80::1%eth0]:1", "[zz]:1", "x:y", "user@host", "[::1]x"):
            self.assertIsNone(g.host_of(bad), bad)

    def test_nova_login_url(self):
        self.assertEqual(g.nova_login_url("192.168.10.10:47995", 47990, "/"),
                         "https://192.168.10.10:47990/login?next=%2Fbrowser")
        url = g.nova_login_url("192.168.10.10:47995", 47990, "/nova/play?app=Desktop&display=mirror")
        self.assertTrue(url.startswith("https://192.168.10.10:47990/login?next=%2Fbrowser%3Fcontinue%3D%252Fnova%252Fplay"))
        self.assertEqual(g.nova_login_url("h:1", 47990, "//evil.com"), "https://h:47990/login?next=%2Fbrowser")
        self.assertIsNone(g.nova_login_url("evil.com/", 47990, "/"))

    def test_safe_path(self):
        self.assertEqual(g.safe_path("/nova/play?app=x"), "/nova/play?app=x")
        for bad in (None, "", "//evil.com/x", "https://evil.com", "/\\evil.com", "evil", "/a b", "/x\r\n"):
            self.assertIsNone(g.safe_path(bad), bad)

    def test_play_target(self):
        self.assertEqual(g.play_target(3, 1234, "virtual"), "/stream.html?hostId=3&appId=1234&novaDisplay=virtual")
        self.assertEqual(g.play_target(3, 1234, None), "/stream.html?hostId=3&appId=1234")
        self.assertEqual(g.play_target(3, 1234, "x&corever=0"), "/stream.html?hostId=3&appId=1234")

    def test_pick_app(self):
        apps = [{"app_id": 1, "title": "Desktop"}, {"app_id": 77, "title": "Hades II"}]
        self.assertEqual(g.pick_app(apps, "Hades II", None)["app_id"], 77)
        self.assertEqual(g.pick_app(apps, "hades ii", None)["app_id"], 77)
        self.assertEqual(g.pick_app(apps, None, "1")["title"], "Desktop")
        self.assertIsNone(g.pick_app(apps, "Nope", None))

    def test_pick_pairing(self):
        pending = [{"id": "lan", "address": "192.168.1.5", "name": "nova-web-client"},
                   {"id": "other", "address": "127.0.0.1", "name": "Moonlight"},
                   {"id": "ours", "address": "::ffff:127.0.0.1", "name": "nova-web-client"}]
        self.assertEqual(g.pick_pairing(pending, "nova-web-client")["id"], "ours")
        self.assertIsNone(g.pick_pairing(pending[:2], "nova-web-client"))

    def test_strip_cookie(self):
        self.assertEqual(g.strip_cookie(f"a=1; nova_session={TOKEN}; b=2", "nova_session"), "a=1; b=2")
        self.assertEqual(g.strip_cookie(f"nova_session={TOKEN}", "nova_session"), "")

    def test_session_cache(self):
        now = [0.0]
        cache = g.SessionCache(10, clock=lambda: now[0], limit=2)
        who = g.Identity("admin", "c", TOKEN)
        cache.put(TOKEN, who)
        self.assertEqual(cache.get(TOKEN), (True, who))
        now[0] = 11
        self.assertEqual(cache.get(TOKEN), (False, None))
        cache.put("x", None)
        cache.put("y", None)
        cache.put("z", None)
        self.assertLessEqual(len(cache.items), 2)

    def test_request_parsing(self):
        req = g.parse_request_head(f"GET /x?a=1 HTTP/1.1\r\nHost: h\r\nCookie: a=1; nova_session={TOKEN}\r\n\r\n".encode(), "1.2.3.4")
        self.assertEqual(req.path, "/x")
        self.assertEqual(req.query, {"a": "1"})
        self.assertEqual(req.cookie(g.NOVA_COOKIE), TOKEN)
        with self.assertRaises(g.HttpError):
            g.parse_request_head(b"GET /\r\n\r\n", "x")
        with self.assertRaises(g.HttpError):
            g.parse_request_head(b"GET / HTTP/1.1\r\nBad Header : x\r\n\r\n", "x")

    def test_mw_config(self):
        defaults = {"data_storage": {"path": "x"}, "web_server": {"bind_address": "0.0.0.0:8080", "certificate": None,
                    "first_login_create_admin": True}, "webrtc": {"ice_servers": [{"urls": ["stun:x"]}]},
                    "moonlight": {}, "log": {}, "streamer_path": "./streamer"}
        mw = g.mw_config(defaults, state_dir="/s", lib_dir="/l", upstream_port=5, udp_min=10, udp_max=20,
                         nova_http_port=47989, forwarded_header="X-H", pair_device_name="nova-web-client")
        self.assertEqual(mw["web_server"]["bind_address"], "127.0.0.1:5")
        self.assertFalse(mw["web_server"]["first_login_create_admin"])
        self.assertEqual(mw["web_server"]["forwarded_header"]["username_header"], "X-H")
        self.assertEqual(mw["webrtc"]["ice_servers"], [])
        self.assertEqual(mw["webrtc"]["port_range"], {"min": 10, "max": 20})
        self.assertEqual(mw["data_storage"]["path"], "/s/data.json")
        self.assertEqual(defaults["web_server"]["bind_address"], "0.0.0.0:8080")  # not modified


class FakeBackends:
    def __init__(self):
        self.paired = False
        self.pair_calls = 0
        self.session_checks = 0
        self.valid = {TOKEN: "admin"}

    def nova_session(self, token):
        self.session_checks += 1
        user = self.valid.get(token)
        return g.Identity(user, "csrf-" + user, token) if user else None

    def paired_host(self, user):
        return {"host_id": 5, "paired": "Paired"} if self.paired else None

    def pair(self, who):
        assert who.csrf == "csrf-admin"
        self.pair_calls += 1
        self.paired = True
        return "paired"

    def mw_json(self, method, path, user, body=None):
        return {"apps": [{"app_id": 42, "title": "Desktop"}]}


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.seen = []

        async def upstream(reader, writer):
            head = await reader.readuntil(b"\r\n\r\n")
            self.seen.append(head.decode())
            if b"Upgrade: websocket" in head:
                writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n")
                await writer.drain()
                while data := await reader.read(100):
                    writer.write(b"echo:" + data)
                    await writer.drain()
            else:
                writer.write(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello")
                await writer.drain()
            writer.close()

        self.up = await asyncio.start_server(upstream, "127.0.0.1", 0)
        up_port = self.up.sockets[0].getsockname()[1]
        self.cfg = dict(g.DEFAULTS, upstream_port=up_port, forwarded_header="X-Nova-Secret-1f2e", revalidate_seconds=0.2,
                        auth_cache_seconds=0.1)
        self.backends = FakeBackends()
        self.gw = g.Gateway(self.cfg, self.backends)
        self.server = await asyncio.start_server(self.gw.handle, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.server.close()
        self.up.close()

    async def request(self, raw: bytes):
        r, w = await asyncio.open_connection("127.0.0.1", self.port)
        w.write(raw)
        await w.drain()
        head = await r.readuntil(b"\r\n\r\n")
        body = b""
        text = head.decode()
        for line in text.split("\r\n"):
            if line.lower().startswith("content-length:"):
                body = await r.readexactly(int(line.split(":")[1]))
        w.close()
        return text, body

    def get(self, path, cookie=TOKEN, extra=""):
        c = f"Cookie: nova_session={cookie}\r\n" if cookie else ""
        return self.request(f"GET {path} HTTP/1.1\r\nHost: 192.168.10.10:47995\r\n{c}{extra}\r\n".encode())

    async def test_signed_out_gets_nova_sign_in_link_and_api_401(self):
        head, body = await self.get("/nova/play?app=Desktop", cookie=None)
        self.assertIn("200 OK", head)
        self.assertIn(b"https://192.168.10.10:47990/login?next=%2Fbrowser%3Fcontinue%3D%252Fnova%252Fplay", body)
        head, _ = await self.get("/api/hosts", cookie=None)
        self.assertIn("401", head)
        head, _ = await self.get("/api/hosts", cookie=OTHER)  # a session Nova doesn't know
        self.assertIn("401", head)
        head, _ = await self.get("/api/hosts", cookie="short")
        self.assertIn("401", head)
        self.assertEqual(self.seen, [])

    async def test_session_is_cached_briefly(self):
        await self.get("/stream.html")
        await self.get("/stream.html")
        self.assertEqual(self.backends.session_checks, 1)
        await asyncio.sleep(0.15)
        await self.get("/stream.html")
        self.assertEqual(self.backends.session_checks, 2)

    async def test_forged_forwarded_user_never_reaches_upstream(self):
        await self.get("/api/hosts", cookie=None, extra="X-Forwarded-User: admin\r\nX-Nova-Secret-1f2e: admin\r\n")
        self.assertEqual(self.seen, [])
        head, body = await self.get("/api/hosts", extra="x-forwarded-user: mallory\r\nX-NOVA-SECRET-1F2E: mallory\r\n"
                                                        "Authorization: Bearer abc\r\nCookie: mws=1\r\n")
        self.assertIn("200 OK", head)
        self.assertEqual(body, b"hello")
        sent = self.seen[-1]
        self.assertIn("X-Nova-Secret-1f2e: admin\r\n", sent)
        self.assertNotIn("X-Forwarded-User", sent)
        self.assertNotIn("mallory", sent)
        self.assertNotIn("Authorization", sent)
        self.assertNotIn(TOKEN, sent)  # Nova's session cookie stays with Nova
        self.assertIn("Cookie: mws=1", sent)

    async def test_api_outside_allowlist_refused(self):
        body = b'{"address":"10.0.0.9","http_port":47989}'
        head, _ = await self.request(f"POST /api/host HTTP/1.1\r\nHost: h\r\nOrigin: https://h\r\nCookie: nova_session={TOKEN}\r\n"
                                     f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
        self.assertIn("403", head)
        head, _ = await self.get("/api/users")
        self.assertIn("403", head)
        self.assertEqual(self.seen, [])

    async def test_first_visit_pairs_and_play_redirects(self):
        head, _ = await self.get("/nova/play?app=Desktop&display=mirror")
        self.assertIn("Location: /stream.html?hostId=5&appId=42&novaDisplay=mirror", head)
        self.assertEqual(self.backends.pair_calls, 1)
        await self.get("/")
        self.assertEqual(self.backends.pair_calls, 1)
        head, _ = await self.get("/nova/play?app=Desktop&display=evil")
        self.assertIn("400", head)
        head, _ = await self.get("/nova/play?app=Nope")
        self.assertIn("404", head)

    async def test_root_pairs_before_proxying(self):
        head, body = await self.get("/")
        self.assertEqual(self.backends.pair_calls, 1)
        self.assertEqual(body, b"hello")

    async def test_websocket_origin_check_tunnel_and_session_end(self):
        ws = (f"GET /api/host/stream HTTP/1.1\r\nHost: x\r\nOrigin: https://evil\r\nCookie: nova_session={TOKEN}\r\n"
              "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: k\r\nSec-WebSocket-Version: 13\r\n\r\n")
        head, _ = await self.request(ws.encode())
        self.assertIn("403", head)
        r, w = await asyncio.open_connection("127.0.0.1", self.port)
        w.write(ws.replace("https://evil", "https://x").encode())
        await w.drain()
        head = await r.readuntil(b"\r\n\r\n")
        self.assertIn(b"101 Switching Protocols", head)
        w.write(b"ping")
        await w.drain()
        self.assertEqual(await r.read(100), b"echo:ping")
        self.assertIn("X-Nova-Secret-1f2e: admin", self.seen[-1])
        # Signing out of Nova closes the stream at the next re-check.
        del self.backends.valid[TOKEN]
        data = await asyncio.wait_for(r.read(100), 3)
        self.assertEqual(data, b"")
        w.close()

    async def test_refuses_peer_outside_policy(self):
        gw = g.Gateway(dict(g.DEFAULTS, allow="lan"), FakeBackends())
        gw.networks = g.networks_for("pc")[1:]  # ::1 only, so 127.0.0.1 is "outside"
        server = await asyncio.start_server(gw.handle, "127.0.0.1", 0)
        port = server.sockets[0].getsockname()[1]
        r, w = await asyncio.open_connection("127.0.0.1", port)
        w.write(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
        await w.drain()
        try:
            data = await r.read(100)
        except ConnectionResetError:
            data = b""
        self.assertEqual(data, b"")  # closed without an answer
        w.close()
        server.close()


if __name__ == "__main__":
    unittest.main()
