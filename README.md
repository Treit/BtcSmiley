# BtcSmiley

A tiny Bitcoin system tray app inspired by StockSmiley. Pure Win32 C,
with no framework, downloaded icons, API key, or third-party libraries.

The yellow face starts in **Bear** mode: happy when Bitcoin falls, sad when
it rises. Right-click the face to select **Bull** mode or exit. Hover over
the face to see the BTC/USD price, percentage change, and selected mode.
The mode resets to Bear when the app restarts.

## Price signal

BtcSmiley compares the latest Coinbase Exchange BTC/USD trade price with
the opening price of its rolling 24-hour window. It refreshes once a minute,
including immediately at startup. A rolling day gives a steadier signal
than an hour and does not need a market closing time or a local price history.
Changes within plus or minus 0.1% show a neutral face.

The app uses the public [Coinbase product stats endpoint](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-stats).
This is one exchange's price, not a market-wide average. Network requests
run on a worker thread. If an update fails, the tooltip marks the last price
as stale and the face keeps its last state. Before the first successful
update, the face is neutral and the tooltip says the price is unavailable.
The app retries each minute and restores its icon if Explorer restarts.

## Build and run

Requires Windows 10 or later and either MSVC's C compiler or MinGW-w64 GCC.
For MSVC, use a Developer PowerShell. For GCC, put `gcc` on `PATH`.

```powershell
.\build.ps1
.\BtcSmiley.exe
```

The executable opens only a tray icon, not a window or console. Windows may
place it in the hidden-icons menu; drag it onto the taskbar to keep it visible.
There is no installer or automatic startup.

Run the focused tests, including parsing, mode inversion, neutral boundaries,
and GDI icon creation:

```powershell
.\build.ps1 -Test
.\tests.exe --live
```

The optional `--live` check also fetches a real quote and requires internet access.
