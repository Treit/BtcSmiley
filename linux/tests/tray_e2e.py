"""Exercise the built app over HTTP and the desktop tray protocols on a private bus."""

import contextlib
import http.server
import os
import pathlib
import queue
import subprocess
import sys
import threading
import time
import unittest

from gi.repository import Gio, GLib


BUS = "org.freedesktop.DBus"
WATCHER = "org.kde.StatusNotifierWatcher"
ITEM = "org.kde.StatusNotifierItem"
MENU = "com.canonical.dbusmenu"

WATCHER_INTERFACE = Gio.DBusNodeInfo.new_for_xml(f"""
<node>
  <interface name="{WATCHER}">
    <method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
    <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
  </interface>
</node>
""").interfaces[0]


def eventually(read, expected):
    deadline = time.monotonic() + 10
    while True:
        actual = read()
        if actual == expected:
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"Expected {expected!r}, got {actual!r}")
        time.sleep(0.025)


class CoinbaseFixture(http.server.ThreadingHTTPServer):
    def __init__(self):
        super().__init__(("127.0.0.1", 0), QuoteRequest)
        self.requests = queue.Queue()
        self.responses = queue.Queue()
        self.url = f"http://127.0.0.1:{self.server_port}/products/BTC-USD/stats"


class QuoteRequest(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.requests.put((self.path, self.headers.get("User-Agent")))
        try:
            status, body = self.server.responses.get(timeout=15)
        except queue.Empty:
            return
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class TrayTest(unittest.TestCase):
    def setUp(self):
        if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
            self.fail("Run the integration suite under dbus-run-session")
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.registrations = queue.Queue()
        self.loop = GLib.MainLoop()
        loop_thread = threading.Thread(target=self.loop.run, daemon=True)
        loop_thread.start()
        self.addCleanup(self.loop.quit)
        self.watcher_object = self.bus.register_object(
            "/StatusNotifierWatcher",
            WATCHER_INTERFACE,
            self.register_item,
            lambda *_: GLib.Variant("b", True),
            None,
        )
        self.addCleanup(self.bus.unregister_object, self.watcher_object)

    def call(self, name, path, interface, method, signature=None, args=()):
        parameters = GLib.Variant(signature, args) if signature else None
        result = self.bus.call_sync(
            name, path, interface, method, parameters, None,
            Gio.DBusCallFlags.NONE, 3000, None,
        )
        return result.unpack()

    def watcher_name(self, owned):
        method, signature, args = (
            ("RequestName", "(su)", (WATCHER, 0))
            if owned else ("ReleaseName", "(s)", (WATCHER,))
        )
        self.assertEqual(self.call(BUS, "/org/freedesktop/DBus", BUS, method, signature, args), (1,))

    def register_item(self, _bus, _sender, _path, _interface, _method, params, invocation):
        self.registrations.put(params.unpack()[0])
        invocation.return_value(None)

    @contextlib.contextmanager
    def launch(self, url=None):
        env = os.environ.copy()
        env.pop("BTCSMILEY_STATS_URL", None)
        env.pop("BTCSMILEY_REFRESH_MS", None)
        if url:
            for key in tuple(env):
                if key.lower().endswith("_proxy"):
                    env.pop(key)
            env.update(BTCSMILEY_STATS_URL=url, BTCSMILEY_REFRESH_MS="200")
        with subprocess.Popen([BINARY], env=env) as process:
            try:
                self.item = self.registrations.get(timeout=10)
                self.menu = self.property("Menu")
                self.assertEqual(self.property("Id"), "btcsmiley")
                self.assertEqual(self.property("Status"), "Active")
                yield process
            finally:
                if process.poll() is None:
                    process.kill()

    def property(self, name):
        return self.call(
            self.item, "/StatusNotifierItem", BUS + ".Properties", "Get",
            "(ss)", (ITEM, name),
        )[0]

    def menu_entries(self):
        _, (_, _, children) = self.call(
            self.item, self.menu, MENU, "GetLayout", "(iias)", (0, 1, []),
        )
        return {props["label"]: (item_id, props) for item_id, props, _ in children if "label" in props}

    def click(self, label):
        item_id, _ = self.menu_entries()[label]
        self.call(self.item, self.menu, MENU, "Event", "(isvu)",
                  (item_id, "clicked", GLib.Variant("i", 0), 0))

    def assert_state(self, quote, mood, mode):
        eventually(lambda: self.property("ToolTip")[3], quote)
        entries = self.menu_entries()
        self.assertFalse(entries[quote][1]["enabled"])
        self.assertEqual(entries["Bear"][1]["toggle-state"], int(mode == "Bear"))
        self.assertEqual(entries["Bull"][1]["toggle-state"], int(mode == "Bull"))
        self.assert_face(mood)

    def assert_face(self, mood):
        pixmaps = self.property("IconPixmap")
        self.assertIn(16, [width for width, _, _ in pixmaps])
        width, height, data = max(pixmaps, key=lambda pixmap: pixmap[0])
        self.assertGreaterEqual(width, 32)
        self.assertEqual(width, height)
        self.assertEqual(len(data), width * height * 4)

        def pixel(x, y):
            offset = ((y * width // 16) * width + x * width // 16) * 4
            return data[offset:offset + 4]

        self.assertEqual(pixel(0, 0)[0], 0)
        self.assertEqual(pixel(8, 8), [255, 255, 220, 0])
        self.assertEqual(pixel(5, 6), [255, 0, 0, 0])
        center_y, corner_y = {"sad": (9, 12), "neutral": (11, 11), "happy": (11, 9)}[mood]
        self.assertEqual(pixel(8, center_y), [255, 0, 0, 0])
        self.assertEqual(pixel(4, corner_y), [255, 0, 0, 0])

    def expect_signal(self, name, action):
        receipt = queue.Queue()
        subscription = self.bus.signal_subscribe(
            self.item, ITEM, name, "/StatusNotifierItem", None, Gio.DBusSignalFlags.NONE,
            lambda *_: receipt.put(True),
        )
        try:
            self.bus.flush_sync(None)
            action()
            receipt.get(timeout=10)
        finally:
            self.bus.signal_unsubscribe(subscription)

    def assert_exit(self, process):
        try:
            self.click("Exit")
        except GLib.Error as error:
            # The item can disappear before the terminal menu event is acknowledged.
            if not error.matches(Gio.dbus_error_quark(), Gio.DBusError.NO_REPLY):
                raise
        self.assertEqual(process.wait(timeout=5), 0)
        self.assertEqual(self.call(BUS, "/org/freedesktop/DBus", BUS, "NameHasOwner",
                                   "(s)", (self.item,)), (False,))

    def test_quote_to_icon_menu_and_watcher_recovery(self):
        self.watcher_name(True)
        self.addCleanup(self.watcher_name, False)
        with CoinbaseFixture() as coinbase:
            thread = threading.Thread(target=coinbase.serve_forever, daemon=True)
            thread.start()
            try:
                with self.launch(coinbase.url) as process:
                    def respond(status, body):
                        self.assertEqual(coinbase.requests.get(timeout=10),
                                         ("/products/BTC-USD/stats", "BtcSmiley/1.0.0"))
                        coinbase.responses.put((status, body))

                    self.assert_state("Bear | Fetching BTC/USD...", "neutral", "Bear")
                    respond(503, b"")
                    self.assert_state("Bear | Price unavailable; retrying", "neutral", "Bear")

                    self.expect_signal("NewIcon", lambda: respond(200, b'{"open":"100","last":"110"}'))
                    self.assert_state("BTC $110.00 | 24h +10.00% | Bear", "sad", "Bear")
                    self.expect_signal("NewIcon", lambda: self.click("Bull"))
                    self.assert_state("BTC $110.00 | 24h +10.00% | Bull", "happy", "Bull")

                    respond(200, b'{"open":"100","last":"invalid"}')
                    self.assert_state("BTC $110.00 | 24h +10.00% | Bull | stale: update failed",
                                      "happy", "Bull")
                    respond(200, b'{"open":"100","last":"99.90"}')
                    self.assert_state("BTC $99.90 | 24h -0.10% | Bull", "neutral", "Bull")
                    respond(200, b'{"open":"100","last":"100.10"}')
                    self.assert_state("BTC $100.10 | 24h +0.10% | Bull", "neutral", "Bull")
                    respond(200, b'{"open":"100","last":"90"}')
                    self.assert_state("BTC $90.00 | 24h -10.00% | Bull", "sad", "Bull")

                    self.watcher_name(False)
                    self.watcher_name(True)
                    self.assertEqual(self.registrations.get(timeout=10), self.item)
                    self.assert_state("BTC $90.00 | 24h -10.00% | Bull", "sad", "Bull")
                    self.click("Bear")
                    self.assert_state("BTC $90.00 | 24h -10.00% | Bear", "happy", "Bear")

                    # Exit even while the next HTTP response is held open.
                    coinbase.requests.get(timeout=10)
                    self.assert_exit(process)
            finally:
                coinbase.shutdown()
                thread.join(timeout=5)

    @unittest.skipUnless(os.environ.get("BTCSMILEY_TEST_LIVE"), "live Coinbase access is opt-in")
    def test_live_coinbase_quote(self):
        self.watcher_name(True)
        self.addCleanup(self.watcher_name, False)
        with self.launch() as process:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                quote = self.property("ToolTip")[3]
                if quote.startswith("BTC $"):
                    break
                time.sleep(0.1)
            self.assertRegex(quote, r"^BTC \$[0-9]+\.[0-9]{2} \| 24h [+-][0-9]+\.[0-9]{2}% \| Bear$")
            self.assertIn(quote, self.menu_entries())
            self.assert_exit(process)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(f"Usage: dbus-run-session -- /usr/bin/python3 {sys.argv[0]} <btcsmiley binary>")
    BINARY = pathlib.Path(sys.argv.pop()).resolve(strict=True)
    unittest.main(verbosity=2)
