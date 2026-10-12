# Installing Pumpkin

Pumpkin puts a Medtronic MiniMed 780G user's glucose on a small pixel clock:
the value, trend, time in range, active insulin and pump alerts (battery,
reservoir, set and sensor change, a stopped pump).

```
780G pump ── MiniMed app (phone) ── CareLink ── Pumpkin bridge ── Pumpkin clock
                                                (Cloudflare)    └─ Nightscout (optional)
```

You set up two things:

1. **The bridge**, a small program on Cloudflare's free plan. It follows the
   patient in CareLink as a care partner, every five minutes.
2. **The clock**, an Ulanzi TC001 with the Pumpkin firmware. It asks the bridge
   for the latest data.

**Time:** about an hour the first time. You need a computer with a terminal
(macOS, Linux or Windows) and you type a few commands; no programming.

> Pumpkin is not a medical device and isn't made or endorsed by Medtronic. It
> uses an unofficial CareLink interface that can change or stop working at any
> time. Never base treatment decisions on the clock alone.

---

## What you need

**Accounts**
- **A CareLink care-partner account, used only by Pumpkin.** Create a new
  care-partner account and link it to the patient, the same way you would add a
  care partner for the CareLink Connect app. Don't reuse a login that's already in
  the CareLink Connect app, Nightscout or anything else that reads CareLink: two
  programs on one login keep logging each other out.
- **A Cloudflare account** (free): <https://dash.cloudflare.com/sign-up>.
- **Optional: Nightscout.** Pumpkin works without it. See
  [Nightscout (optional)](#nightscout-optional).

**On the computer**
- [Node.js](https://nodejs.org) 20 or newer (`node --version`)
- [Google Chrome](https://www.google.com/chrome/), used for the CareLink login
- [Git](https://git-scm.com) and Python 3, for building the clock firmware
- A terminal: Terminal on macOS, PowerShell on Windows

**Hardware**
- An Ulanzi TC001 pixel clock
- A USB-C **data** cable (the one in the box is sometimes charge-only)
- A 2.4 GHz Wi-Fi network for the clock (5 GHz-only networks don't work)

---

## 1. Get the code

```bash
git clone https://github.com/biscuitj/pumpkin.git
cd pumpkin/bridge
npm install
```

`npm install` fetches the tools the bridge needs (Wrangler, Cloudflare's
command-line tool, and the login helper). It doesn't download a browser.

Every command below runs from this `bridge` folder unless it says otherwise.

## 2. Connect Wrangler to Cloudflare

```bash
npx wrangler login
```

A browser window asks you to allow Wrangler access to your Cloudflare account.
Allow it. Check with `npx wrangler whoami`.

## 3. Make your configuration

```bash
cp wrangler.toml.example wrangler.toml
```

On Windows PowerShell: `copy wrangler.toml.example wrangler.toml`.

Open `wrangler.toml` in a text editor. For now, check these lines:

| line | set it to |
|---|---|
| `name` | the bridge's name; it becomes part of its address. `pumpkin-bridge` is fine. |
| `CARELINK_COUNTRY` | the country of the CareLink account, as a two-letter code (`BE`, `NL`, `DE`, `GB`, `US` …) |
| `LANG` | `en` or `nl`: language of the short alert texts |
| `TARGET_LOW`, `TARGET_HIGH` | the time-in-range limits in mg/dL. In mmol/L: multiply by 18 (3.9 → 70, 10.0 → 180). |

Leave `NS_URL` empty unless you set up Nightscout later.

## 4. Create the storage

The bridge keeps its CareLink login and the latest data in Cloudflare's key-value
storage (KV).

```bash
npx wrangler kv namespace create CARELINK
```

The output ends with a block like:

```toml
[[kv_namespaces]]
binding = "CARELINK"
id = "3f2a9c0d…"
```

If Wrangler offers to add it to your configuration, answer **no**: the block
is already in `wrangler.toml`, and a second copy breaks the file. Copy the `id`
value into `wrangler.toml`, in place of `<your KV namespace id>`.

## 5. Log in to CareLink

```bash
npm run login
```

If the CareLink account isn't Belgian, add the country:
`npm run login -- --country NL`. For a US account: `npm run login -- --us`.

1. Chrome opens on the CareLink login page, in a separate window.
2. Log in with the **care-partner account** from *What you need*, not the
   patient's own account.
3. Solve the captcha.
4. The window closes by itself. The terminal says `Storing the tokens in the
   Worker…` and ends with `Done.`

The login is stored in the bridge's storage; nothing is left on your computer.
You only do this once: the bridge keeps the login alive. You'd only repeat it
after the bridge was off for more than a week, or when CareLink asks for a new
login.

If it fails, see [Troubleshooting](#troubleshooting).

## 6. Make the clock token

The clock proves itself to the bridge with a long random password, the *clock
token*. Make one:

```bash
openssl rand -hex 24
```

No `openssl` on Windows? Use
`node -e "console.log(require('crypto').randomBytes(24).toString('hex'))"`.

Keep the token in your password manager; you'll enter it in the clock in step 9.
Then give it to the bridge:

```bash
npx wrangler secret put CLOCK_TOKEN
```

Paste the token when asked and press Enter. (On a brand-new Cloudflare
account Wrangler may first ask to create the Worker; answer yes.)

## 7. Deploy the bridge

```bash
npx wrangler deploy
```

The first time, Wrangler may ask you to pick a `workers.dev` subdomain for your
account; any free name works. At the end it prints the bridge's address, for
example:

```
https://pumpkin-bridge.your-subdomain.workers.dev
```

That's your **bridge address**. The output should also list the `CARELINK` KV
binding and the schedule `*/30 * * * *`.

## 8. Check the bridge

Put the address and token in two variables (macOS / Linux):

```bash
export BRIDGE=https://pumpkin-bridge.your-subdomain.workers.dev
export CLOCK_TOKEN=paste-the-token
```

On Windows PowerShell: `$env:BRIDGE="https://…"` and `$env:CLOCK_TOKEN="…"`,
and write `$env:BRIDGE` / `$env:CLOCK_TOKEN` in the commands below.

Force a first CareLink fetch:

```bash
curl -s -X POST -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/carelink/poll
```

The answer is a long JSON text. Look for:

| in the answer | should be |
|---|---|
| `"error"` under `carelink` | `null` |
| `"last"` under `snapshot` | the current glucose and time |
| `"iob"` | the active insulin the pump shows |
| `"source"` under `clock` | `"carelink"` |

Then what the clock will get:

```bash
curl -s -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/clock
```

You should see the glucose, trend and time in range. Time in range can be `-1`
for the first day, until the bridge has seen enough readings.

## 9. Install the firmware on the clock

This uses PlatformIO, the tool that builds and flashes the firmware. From the
`pumpkin` folder:

```bash
cd ../firmware
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements-build.txt
pio run -t upload
```

The `.venv` folder keeps PlatformIO apart from the rest of your system. In a new
terminal, run the `activate` line again before using `pio`.

Connect the clock with the USB-C cable first, directly to the computer (not
through a hub). The first build downloads the ESP32 toolchain and takes a few
minutes. If the upload can't find the clock or times out:

- Windows: install the [CH340 driver](https://sparks.gogo.co.nz/ch340.html).
- Hold the clock's **middle button** while plugging in the cable, release it
  after a second, and run `pio run -t upload` again.

Want to keep the original Ulanzi firmware? Back it up first; see
[`firmware/INSTALL.md`](../firmware/INSTALL.md), step 1.

After the upload the clock restarts, shows a pumpkin and then its setup
network name.

## 10. Connect the clock to Wi-Fi

1. On a phone or laptop, join the open network `Pumpkin-Setup-XXXX` that the
   clock shows.
2. If a login window pops up, use it to open a normal browser, and go to
   `http://192.168.4.1`.
3. On the **Wi-Fi** tab, choose your 2.4 GHz network, enter the password and
   choose **Join this network**.
4. The clock tests the network before saving it, then scrolls its new address,
   for example `pumpkin-a1b2.local` or an IP address like `192.168.0.12`.

School or company Wi-Fi with a username (WPA2-Enterprise) works too; see
[`firmware/INSTALL.md`](../firmware/INSTALL.md), *School and WPA2-Enterprise
WiFi*.

## 11. Point the clock at the bridge

Back on your normal network, open the clock's address in a browser
(`http://pumpkin-a1b2.local`, or the IP address). Forgot it? Double-press the
clock's middle button.

On the **Blood Sugar** tab:

| field | value |
|---|---|
| Data Source | **Custom URL** |
| Server URL | your bridge address + `/clock`, e.g. `https://pumpkin-bridge.your-subdomain.workers.dev/clock` |
| Auth Token | the clock token from step 6 |

Choose **Save Configuration**. Within a minute the clock shows the glucose.

Then, while you're there:
- **Clocks** tab: give the clock a name (its address becomes `name.local`) and,
  if you like, a password for this web page (user `pumpkin`).
- **Display** tab: night mode, brightness. *Full Mute* turns the screen and
  sound off completely during the night hours, for example at school after
  hours.
- **Blood Sugar** tab: glucose thresholds and colours.

A second clock? Do steps 9 and 10 for it, then on the first clock's **Clocks**
tab tick the new one under *Copy Settings to Other Clocks*, include the data
source, and copy.

---

## Day to day

- **Updating the clock:** build with `pio run` in `firmware/` and upload
  `firmware/.pio/build/esp32dev/firmware.bin` in the clock's web page
  (**Device → Update firmware**). Settings stay. If the new version doesn't start
  properly, the clock goes back to the previous one by itself.
- **Updating the bridge:** `git pull`, then in `bridge/`: `npm install` and
  `npx wrangler deploy`. Your `wrangler.toml` is never overwritten by `git pull`.
- **`CARELINK LOGIN NEEDED`** in `/clock` (`bridge_alert`): CareLink rejected the
  login, usually because the bridge was off for over a week, or the care-partner
  password changed. Run `npm run login` again; no deploy needed.
- **`CARELINK ERROR`**: three or more failed fetches in a row, usually CareLink
  itself. The bridge retries on its own (after 1, 2, 4 … up to 30 minutes).
- **The clock shows no data or stale data:** check the MiniMed app on the phone
  first. If that's current, look at the bridge:
  `curl -s -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/debug`.

Cloudflare's free plan is plenty: one clock uses about 300 requests and 330
storage writes a day; the limits are 100,000 and 1,000.

## Nightscout (optional)

With Nightscout, the bridge can do two more things:

- **Fallback:** when CareLink is down, the clock gets its glucose from Nightscout.
- **Upload:** the bridge sends readings, pump IOB, reservoir, battery, boluses,
  carbs and temp targets to Nightscout. It then replaces Nightscout's own
  CareLink connection.

In `wrangler.toml`:

```toml
NS_URL = "https://your-nightscout.example.com"
NS_UPLOAD = "true"      # only for the upload
```

Secrets:

```bash
npx wrangler secret put NS_TOKEN        # a read token from Nightscout (Admin Tools → Subjects, role "readable")
npx wrangler secret put NS_API_SECRET   # only for the upload: Nightscout's API_SECRET
```

Instead of the API secret you can use a token with the `admin` role:
`npx wrangler secret put NS_UPLOAD_TOKEN`. Then `npx wrangler deploy`.

**Before you turn the upload on, turn off Nightscout's own CareLink
connection** (the MiniMed / CareLink "connect" plugin). Otherwise Nightscout
gets everything twice, and if it uses the same CareLink login the two log each
other out.

Check after the next reading:
`curl -s -X POST -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/carelink/poll`
should show `"nightscout_upload"` with `"ok": true`.

## Troubleshooting

| symptom | cause | fix |
|---|---|---|
| `npm run login`: `could not start Google Chrome` | Chrome isn't installed, or a hanging Chrome | install Chrome; close all Chrome windows and try again |
| `npm run login`: the window stays on the login page | wrong account, or a step CareLink added (e.g. accepting new terms) | finish whatever the page asks; log in with the care-partner account |
| `npm run login`: `wrangler could not store the tokens` | not logged in to Cloudflare, or not run in `bridge/` | `npx wrangler whoami`; `cd bridge`; check the KV `id` in `wrangler.toml` |
| `npm run login`: `non-Auth0 login` | CareLink uses another login system in that region | not supported yet |
| `/carelink/poll`: `no CareLink tokens in KV` | the login wasn't stored, or the KV `id` changed | run `npm run login` again |
| `/carelink/poll`: `token refresh rejected` | the login is used elsewhere too, or older than a week | make sure only Pumpkin uses this care-partner login; `npm run login` |
| `/carelink/poll`: `no linked patient found` | the care-partner account isn't linked to the patient | check the link in CareLink; if it is linked, set `CARELINK_USERNAME` (care-partner login) and `CARELINK_PATIENT_USERNAME` (the patient's CareLink username) in `wrangler.toml` and deploy |
| `/clock` answers `401` | wrong token, or a space pasted along | `npx wrangler secret put CLOCK_TOKEN` again and enter the same value in the clock |
| times an hour or two off | the pump's clock or time zone | check the pump's time; send the `/debug` output to whoever helps you |
| clock: `NO WIFI` | wrong password, or a 5 GHz-only network | the setup network comes back after about two minutes; join it and choose the network again |
| clock: `NO DATA` | wrong Server URL or token | check both on the Blood Sugar tab; test with the `curl` from step 8 |
| forgot the clock's web password | | only a USB reset clears it: in `firmware/`, `pio run -t erase` then `pio run -t upload`, and set the clock up again |

More on the clock (buttons, screens, recovery):
[`firmware/INSTALL.md`](../firmware/INSTALL.md). More on the bridge:
[`bridge/README.md`](../bridge/README.md).

## Coming from an earlier Pumpkin version

- **Bridge from before 0.9:** run `npm install` once (it adds the new login
  tool) and deploy. The old login with carelink-python-client is no longer
  needed; next time a login is due, use `npm run login`. Add `LANG = "nl"` to
  `wrangler.toml` to keep the Dutch alert texts (the default is now English).
  Your Worker keeps its name and address; nothing changes on the clock.
- **Clock on SugarClock or Pumpkin 0.7 and older:** flash once over USB
  (step 9) **without erasing**; settings stay. From 0.8 on, updates go through
  the web page.
