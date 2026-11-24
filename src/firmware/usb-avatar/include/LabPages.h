// Goal: Provide the local Wi-Fi lab's built-in web pages.
// Responsibilities: Store the HTML, styles, and scripts served by the platform.
// How it works:
// 1. The platform selects the appropriate page fragments.
// 2. HTTP handlers serve those fragments to a local browser.
// 3. Page controls interact with the local firmware API.
#pragma once

namespace lab {

inline constexpr const char* pageHead = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>NET Bot local lab</title>
<script>
(() => {
  const param = new URLSearchParams(window.location.search).get("scoutTheme");
  const theme =
    param || (window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
  document.documentElement.setAttribute("data-theme", theme);
})();
</script>
<style>
:root {
  color-scheme: light;
  --cp-bg: #f7f4ef;
  --cp-bg-elevated: #fcfbf8;
  --cp-surface: #ffffff;
  --cp-surface-soft: #f5f5f5;
  --cp-border: #dedede;
  --cp-border-strong: #919191;
  --cp-text: #242424;
  --cp-text-muted: #5c5c5c;
  --cp-text-soft: #6f6f6f;
  --cp-accent: #b11f4b;
  --cp-accent-hover: #9a1a41;
  --cp-accent-soft: rgba(177, 31, 75, 0.08);
  --cp-accent-fg: #ffffff;
  --cp-success: #16a34a;
  --cp-danger: #dc2626;
  --cp-warning: #f59e0b;
  --cp-link: #0078d4;
  --cp-shadow: 0 18px 48px rgba(0, 0, 0, 0.12);
  --cp-overlay: rgba(255, 255, 255, 0.8);
  --cp-panel: rgba(255, 255, 255, 0.86);
  --cp-panel-strong: rgba(255, 255, 255, 0.96);
  --cp-sheen: rgba(255, 255, 255, 0.55);
  --cp-highlight: rgba(177, 31, 75, 0.12);
}
html[data-theme="dark"] {
  color-scheme: dark;
  --cp-bg: #3d3b3a;
  --cp-bg-elevated: #343231;
  --cp-surface: #292929;
  --cp-surface-soft: #2e2e2e;
  --cp-border: #474747;
  --cp-border-strong: #5f5f5f;
  --cp-text: #dedede;
  --cp-text-muted: #919191;
  --cp-text-soft: #b0b0b0;
  --cp-accent: #fd8ea1;
  --cp-accent-hover: #fb7b91;
  --cp-accent-soft: rgba(253, 142, 161, 0.14);
  --cp-accent-fg: #1a1a1a;
  --cp-success: #4ade80;
  --cp-danger: #f87171;
  --cp-warning: #fbbf24;
  --cp-link: #4da6ff;
  --cp-shadow: 0 18px 48px rgba(0, 0, 0, 0.32);
  --cp-overlay: rgba(41, 41, 41, 0.88);
  --cp-panel: rgba(41, 41, 41, 0.72);
  --cp-panel-strong: rgba(41, 41, 41, 0.96);
  --cp-sheen: rgba(255, 255, 255, 0.04);
  --cp-highlight: rgba(253, 142, 161, 0.12);
}
body { margin: 0; background: var(--cp-bg); color: var(--cp-text);
  font-family: "Segoe UI", Aptos, Calibri, -apple-system, BlinkMacSystemFont, sans-serif; }
main { max-width: 28rem; margin: 2rem auto; padding: 1rem; }
form { padding: 1rem; background: var(--cp-surface); border: 1px solid var(--cp-border); border-radius: 16px; }
label { display: block; margin: 1rem 0; }
input { display: block; width: 100%; box-sizing: border-box; padding: .75rem;
  background: var(--cp-surface); color: var(--cp-text); border: 1px solid var(--cp-border-strong); border-radius: .625rem; }
input[type="range"] { padding: 0; accent-color: var(--cp-accent); }
button { padding: .75rem; background: var(--cp-accent); color: var(--cp-accent-fg);
  border: 0; border-radius: .625rem; cursor: pointer; }
button:disabled { background: var(--cp-surface-soft); color: var(--cp-text-muted); }
a { color: var(--cp-link); }
p { overflow-wrap: anywhere; line-height: 1.5; }
</style></head><body><main>)HTML";

inline constexpr const char* diagnosticPage = R"HTML(
<h1>NET Bot lab</h1><p>Local HTTP lab. No API authentication or encryption.</p>
<p><a href="/api/status">Status JSON</a> | <a href="/api/network">Network JSON</a> | <a href="/setup">Wi-Fi setup</a></p>
<section aria-labelledby="brightness-heading">
<h2 id="brightness-heading">Display brightness</h2>
<p>Reported values are cached driver settings, not measured luminance.
Hardware writes are unverified. Completion means only that the software request finished.</p>
<label for="brightness">Selected driver setting <output id="brightness-value" for="brightness">32</output></label>
<input id="brightness" type="range" min="32" max="192" step="1" value="32"
  aria-describedby="brightness-help">
<p id="brightness-help">Allowed range: 32 to 192. Changes are not saved; the default is restored after restart.</p>
<button id="brightness-apply" type="button">Apply brightness</button>
<p id="brightness-result" role="status" aria-live="polite"></p>
</section>
<p>Use the local C# panel for actions. Touch the robot for Demo, Quiet and Stop.</p>
<script>
const brightness = document.querySelector("#brightness");
const brightnessValue = document.querySelector("#brightness-value");
const brightnessApply = document.querySelector("#brightness-apply");
const brightnessResult = document.querySelector("#brightness-result");
const wait = milliseconds => new Promise(done => setTimeout(done, milliseconds));

function showBrightness(state) {
  brightness.min = state.minimum;
  brightness.max = state.maximum;
  const value = state.pending ? state.requestedBrightness : state.brightness;
  brightness.value = value;
  brightnessValue.value = value;
  brightnessValue.textContent = value;
}

async function readBrightness() {
  const response = await fetch("/api/display/brightness", {cache: "no-store"});
  const state = await response.json();
  if (!response.ok) throw new Error(state.error || "brightness_unavailable");
  return state;
}

async function waitForBrightness(target) {
  for (let attempt = 0; attempt < 30; attempt++) {
    await wait(100);
    const state = await readBrightness();
    showBrightness(state);
    if (!state.pending) {
      if (state.error) return "The cached driver setting did not match the request. Hardware write is unverified.";
      return state.brightness === target ?
        "Software request completed. Cached driver setting: " + state.brightness + ". Hardware write is unverified." :
        "Cached driver setting differs from the requested value. Hardware write is unverified.";
    }
  }
  return "Brightness is still pending. Read status before applying another change.";
}

brightness.addEventListener("input", () => { brightnessValue.value = brightness.value;
  brightnessValue.textContent = brightness.value; });
brightnessApply.addEventListener("click", async () => {
  const target = Number(brightness.value);
  brightnessApply.disabled = true;
  brightnessResult.textContent = "Requesting brightness...";
  try {
    const response = await fetch("/api/display/brightness", {method: "POST",
      headers: {"Content-Type": "application/json"}, body: JSON.stringify({brightness: target})});
    const state = await response.json();
    if (response.status !== 202) {
      brightnessResult.textContent = state.error === "brightness_busy" ?
        "Another brightness change is pending. Read status before trying again." :
        "Brightness was not accepted. Check the displayed range and try again.";
      return;
    }
    showBrightness(state);
    brightnessResult.textContent = await waitForBrightness(target);
  } catch {
    brightnessResult.textContent = "The result is uncertain. Read status before applying another change.";
  } finally { brightnessApply.disabled = false; }
});

readBrightness().then(state => {
  showBrightness(state);
  brightnessResult.textContent = state.pending ? "A brightness change is pending." :
    "Cached driver setting: " + state.brightness + ". Hardware write is unverified.";
}).catch(() => { brightnessResult.textContent = "Cached driver setting is unavailable."; });
</script>
)HTML";

inline constexpr const char* setupPage = R"HTML(
<h1>Wi-Fi setup</h1><p>Temporary setup, opened by touching the robot.
Use a trusted 2.4 GHz lab network. Do not record credentials.</p>
<form autocomplete="off"><label>Network name<input id="ssid" maxlength="32" required autocomplete="off"></label>
<label>Network password<input id="password" type="password" maxlength="63" autocomplete="new-password"></label>
<button>Connect</button></form><p id="result" role="status"></p>
<p>Robot Wi-Fi icon: blue is setup open, amber is connecting, green is connected, red is failed, gray is off.</p>
<p>Settings are saved only after connection. This page closes when setup ends. Cancel from the robot.</p>
<script>
const result = document.querySelector("#result");
const wait = milliseconds => new Promise(done => setTimeout(done, milliseconds));

async function sendSettings() {
  const body = JSON.stringify({ssid: document.querySelector("#ssid").value,
    password: document.querySelector("#password").value});
  document.querySelector("#password").value = "";
  const response = await fetch("/api/config", {method: "POST",
    headers: {"Content-Type": "application/json"}, body});
  return response.status;
}

async function readNetwork() {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 3000);
  try {
    const response = await fetch("/api/network", {cache: "no-store", signal: controller.signal});
    return await response.json();
  } finally { clearTimeout(timer); }
}

function explain(status) {
  if (status.state === "connected")
    return "Connected. The robot closes this setup network. Reconnect your phone to your normal Wi-Fi.";
  if (status.error === "connection_failed")
    return "Could not connect. Check the network name and password, then try again.";
  if (status.error) return "The robot reported " + status.error + ". Check Network on the robot.";
  if (!status.setup) return "Setup ended on the robot. Check its Wi-Fi icon.";
  return "The robot is not connecting. Check Network on the robot.";
}

// A successful connection closes the setup network, so the last poll usually fails.
async function watchConnection() {
  const began = Date.now();
  while (Date.now() - began < 30000) {
    await wait(1000);
    let status;
    try { status = await readNetwork(); } catch {
      return "The robot left the setup network. If its Wi-Fi icon is green, setup worked: " +
        "reconnect your phone to your normal Wi-Fi. If it is red, touch the robot, open Network and run setup again.";
    }
    if (status.state !== "connecting") return explain(status);
  }
  return "Still connecting. Check the Wi-Fi icon on the robot.";
}

document.querySelector("form").addEventListener("submit", async event => {
  event.preventDefault();
  const button = document.querySelector("button");
  button.disabled = true;
  result.textContent = "Sending settings...";
  try {
    if (await sendSettings() === 202) {
      result.textContent = "Connecting...";
      result.textContent = await watchConnection();
    } else result.textContent = "Settings not accepted. Check Network on the robot.";
  } catch {
    result.textContent = "Result uncertain. Check the robot; settings are not resent automatically.";
  } finally { button.disabled = false; }
});
</script>)HTML";

}  // namespace lab
