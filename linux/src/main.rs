mod face;

use std::sync::mpsc::{self, Sender};
use std::time::{Duration, Instant};
use std::{env, error::Error, thread};

use face::Mood;
use ksni::blocking::{Handle, TrayMethods};
use ksni::menu::{CheckmarkItem, StandardItem};
use ksni::{Icon, MenuItem, ToolTip, Tray};
use serde::Deserialize;

const STATS_URL: &str = "https://api.exchange.coinbase.com/products/BTC-USD/stats";
const REFRESH_INTERVAL: Duration = Duration::from_secs(60);

#[derive(Deserialize)]
struct ProductStats {
    open: String,
    last: String,
}

struct Quote {
    price: f64,
    change: f64,
}

impl Quote {
    fn fetch(agent: &ureq::Agent, url: &str) -> Option<Self> {
        let mut response = agent.get(url).call().ok()?;
        if response.status() != 200 {
            return None;
        }
        let body = response
            .body_mut()
            .with_config()
            .limit(4095)
            .read_to_vec()
            .ok()?;
        let stats: ProductStats = serde_json::from_slice(&body).ok()?;
        let open = stats.open.parse::<f64>().ok()?;
        let price = stats.last.parse::<f64>().ok()?;
        if !open.is_finite() || !price.is_finite() || open <= 0.0 || price <= 0.0 {
            return None;
        }
        let change = (price / open - 1.0) * 100.0;
        change.is_finite().then_some(Self { price, change })
    }
}

#[derive(Clone, Copy, Default, PartialEq, Eq)]
enum Mode {
    #[default]
    Bear,
    Bull,
}

impl Mode {
    fn label(self) -> &'static str {
        match self {
            Self::Bear => "Bear",
            Self::Bull => "Bull",
        }
    }

    fn mood(self, change: f64) -> Mood {
        let favorable = match self {
            Self::Bear => -change,
            Self::Bull => change,
        };
        // Allow for rounding in the price ratio at exactly +/-0.1%.
        const NEUTRAL_LIMIT: f64 = 0.1 + 100.0 * f64::EPSILON;
        if favorable > NEUTRAL_LIMIT {
            Mood::Happy
        } else if favorable < -NEUTRAL_LIMIT {
            Mood::Sad
        } else {
            Mood::Neutral
        }
    }
}

struct Indicator {
    mode: Mode,
    quote: Option<Quote>,
    stale: bool,
    exit: Sender<()>,
}

impl Indicator {
    fn update_quote(&mut self, result: Option<Quote>) {
        self.stale = result.is_none();
        if result.is_some() {
            self.quote = result;
        }
    }

    fn quote_text(&self) -> String {
        let mode = self.mode.label();
        match &self.quote {
            Some(quote) => {
                let stale = if self.stale {
                    " | stale: update failed"
                } else {
                    ""
                };
                format!(
                    "BTC ${:.2} | 24h {:+.2}% | {mode}{stale}",
                    quote.price, quote.change
                )
            }
            None if self.stale => format!("{mode} | Price unavailable; retrying"),
            None => format!("{mode} | Fetching BTC/USD..."),
        }
    }
}

impl Tray for Indicator {
    const MENU_ON_ACTIVATE: bool = true;

    fn id(&self) -> String {
        "btcsmiley".into()
    }

    fn title(&self) -> String {
        "BtcSmiley".into()
    }

    fn icon_pixmap(&self) -> Vec<Icon> {
        self.quote
            .as_ref()
            .map_or(Mood::Neutral, |quote| self.mode.mood(quote.change))
            .icons()
    }

    fn tool_tip(&self) -> ToolTip {
        ToolTip {
            title: self.title(),
            description: self.quote_text(),
            ..Default::default()
        }
    }

    fn menu(&self) -> Vec<MenuItem<Self>> {
        let mut menu = vec![
            StandardItem {
                label: self.quote_text(),
                enabled: false,
                ..Default::default()
            }
            .into(),
            MenuItem::Separator,
        ];
        menu.extend([Mode::Bear, Mode::Bull].map(|mode| {
            CheckmarkItem {
                label: mode.label().into(),
                checked: self.mode == mode,
                activate: Box::new(move |tray: &mut Self| tray.mode = mode),
                ..Default::default()
            }
            .into()
        }));
        menu.extend([
            MenuItem::Separator,
            StandardItem {
                label: "Exit".into(),
                activate: Box::new(|tray: &mut Self| {
                    let _ = tray.exit.send(());
                }),
                ..Default::default()
            }
            .into(),
        ]);
        menu
    }
}

fn refresh_quotes(tray: Handle<Indicator>, url: &str, interval: Duration) {
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(15)))
        .max_redirects(0)
        .user_agent(concat!("BtcSmiley/", env!("CARGO_PKG_VERSION")))
        .tls_config(
            ureq::tls::TlsConfig::builder()
                .root_certs(ureq::tls::RootCerts::PlatformVerifier)
                .build(),
        )
        .build()
        .into();
    while !tray.is_closed() {
        let started = Instant::now();
        let result = Quote::fetch(&agent, url);
        if tray.update(|tray| tray.update_quote(result)).is_none() {
            break;
        }
        thread::sleep(interval.saturating_sub(started.elapsed()));
    }
}

fn run() -> Result<(), Box<dyn Error>> {
    let url = env::var("BTCSMILEY_STATS_URL").unwrap_or_else(|_| STATS_URL.into());
    let interval = match env::var("BTCSMILEY_REFRESH_MS") {
        Ok(value) => {
            let millis = value.parse::<u64>()?;
            if millis == 0 {
                return Err("BTCSMILEY_REFRESH_MS must be positive".into());
            }
            Duration::from_millis(millis)
        }
        Err(env::VarError::NotPresent) => REFRESH_INTERVAL,
        Err(error) => return Err(error.into()),
    };
    let (exit, wait_for_exit) = mpsc::channel();
    let tray = Indicator {
        mode: Mode::default(),
        quote: None,
        stale: false,
        exit,
    }
    .spawn()
    .map_err(|error| {
        format!("Could not start the tray indicator. On Ubuntu, enable Ubuntu AppIndicators and run from your desktop session. {error}")
    })?;

    let worker_tray = tray.clone();
    thread::Builder::new()
        .name("coinbase-quotes".into())
        .spawn(move || refresh_quotes(worker_tray, &url, interval))?;

    wait_for_exit.recv()?;
    tray.shutdown().wait();
    Ok(())
}

fn main() -> std::process::ExitCode {
    match run() {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("BtcSmiley: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
