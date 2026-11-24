# BtcSmiley

A tiny Bitcoin system tray app inspired by StockSmiley. Available for
Linux in Rust and Windows in Win32 C. Both versions draw their own icons
and use Coinbase's public API without an API key.

The yellow face starts in **Bear** mode and smiles when Bitcoin falls or
frowns when it rises. Open the face's menu to select **Bull** mode, which
reverses the reaction, or to exit. The mode resets to Bear when the app restarts.

## Repository layout

| Platform | Source | Tests | Build output |
| --- | --- | --- | --- |
| Linux (Rust) | `linux/src/` | `linux/tests/` | `linux/target/` |
| Windows (Win32 C) | `windows/src/` | `windows/tests/` | `windows/build/` |

## Price signal

BtcSmiley compares the latest Coinbase Exchange BTC/USD trade price with
the opening price of its rolling 24-hour window. It refreshes once a minute,
including immediately at startup. A rolling day gives a steadier signal
than an hour and does not need a market closing time or a local price history.
Changes within plus or minus 0.1% show a neutral face.

The app uses the public [Coinbase product stats endpoint](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-stats).
This is one exchange's price, not a market-wide average. Network requests
run in the background. If an update fails, BtcSmiley marks the last price
as stale and keeps the last mood. The face starts neutral and the status
reads “Fetching BTC/USD…” until the first result. If that request fails,
the status reports that the price is unavailable. The app retries each
minute and restores its icon when the tray host restarts.

## Ubuntu

Start at the repository root in an Ubuntu desktop terminal. Use Rust 1.89
or later and a C linker.

```sh
sudo apt install build-essential ca-certificates
cargo run --manifest-path linux/Cargo.toml --release --locked
```

BtcSmiley appears in the GNOME **top bar** through Ubuntu AppIndicators.
If needed, enable the extension in the Extensions app or run
`gnome-extensions enable ubuntu-appindicators@ubuntu.com`. If the extension is
missing, install `gnome-shell-ubuntu-extensions` on Ubuntu 26.04 or
`gnome-shell-extension-appindicator` on Ubuntu 24.04, then log out and back in.

Click the face to see the BTC/USD quote, signed 24-hour change, and mode.
Ubuntu AppIndicators displays these details in the menu. On tray hosts
that support StatusNotifierItem tooltips, you can also hover over the face.
Ubuntu Dock's sidebar is for launchers and windows. For a vertical tray,
use a KDE Plasma side panel with its System Tray widget.
[Ubuntu AppIndicators](https://github.com/ubuntu/gnome-shell-extension-appindicator)
· [KDE Plasma panels](https://userbase.kde.org/Plasma/Panels)

### Linux end-to-end tests

The tests launch the real binary with a private D-Bus session and a local
HTTP fixture. They drive the tray menu, inspect the rendered face data,
and check that the icon returns when the tray host restarts.

```sh
sudo apt install dbus-daemon python3-gi
cargo build --manifest-path linux/Cargo.toml --locked
dbus-run-session -- /usr/bin/python3 linux/tests/tray_e2e.py linux/target/debug/btcsmiley
```

Set `BTCSMILEY_TEST_LIVE=1` before `dbus-run-session` to also test the live
Coinbase request. The local fixture uses `BTCSMILEY_STATS_URL` and
`BTCSMILEY_REFRESH_MS` to override the endpoint and refresh interval.

## Windows

Requires Windows 10 or later and either MSVC's C compiler or MinGW-w64 GCC.
For MSVC, use a Developer PowerShell. For GCC, put `gcc` on `PATH`.
Run these commands from the repository root.

```powershell
.\windows\build.ps1
.\windows\build\BtcSmiley.exe
```

The executable opens a tray icon. Right-click for **Bear**, **Bull**, and
**Exit**, or hover for the BTC/USD quote, percentage change, and mode.
Windows may place it in the hidden-icons menu. Drag it onto the taskbar
to keep it visible.

Run the focused tests, including parsing, mode inversion, neutral boundaries,
and GDI icon creation:

```powershell
.\windows\build.ps1 -Test
.\windows\build\tests.exe --live
```

The optional `--live` check also fetches a real quote and requires internet access.
