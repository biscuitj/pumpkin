# SugarClock bridge: installation

This takes the Worker from Nightscout-only to being the only CareLink client,
with Nightscout (ns10be) fed by the Worker.

**Time:** about 30 minutes. **Gap in Nightscout:** a few minutes, between steps 6
and 9, and the readings are filled in afterwards.
**The clock:** same URL. It does get a new token (step 2), which you enter once in
its web interface.

---

## What you need

- The Mac with `~/Sites/pumpkin/bridge`, where you already ran `wrangler login`
  and deployed. The secret `NS_TOKEN` is already set; `CLOCK_TOKEN` gets
  replaced in step 2.
- The login (email and password) of **ns10be's dedicated care-partner account**.
  It must not have MFA turned on; the login script doesn't support it.
- Access to the ns10be dashboard.
- Google Chrome, which the login script drives (after a one-line change, step 5).
- The clock on your Wi-Fi, so you can open its web interface.
- A clone of the `pumpkin` repo in `~/Sites/pumpkin`.

Use two terminal windows:
- **Terminal A** for the Worker (steps 1–4 and 8–10). Step 2 sets
  `$CLOCK_TOKEN` and `$BRIDGE` there, and the later `curl` commands use them.
- **Terminal B** for the login script (steps 5 and 7).

---

## 1. Put the new code in place

```bash
cd ~/Sites/pumpkin/bridge
cp -n wrangler.toml.example wrangler.toml   # first time only; fill in the <…> values
npm install
npm test
```

`npm test` should end with `# pass 65` and `# fail 0`.

`unzip -o` overwrites the source files but leaves `node_modules` and anything
else that isn't in the zip. If you had changed `wrangler.toml` yourself, redo
those changes; the zip has a new version of it.

## 2. New clock token

Cloudflare secrets can't be read back, so make a new token. In terminal A:

```bash
cd ~/Sites/pumpkin/bridge
export CLOCK_TOKEN=$(openssl rand -hex 24)
export BRIDGE=https://sugarclock-bridge.emve.workers.dev
echo "$CLOCK_TOKEN"
```

Store it somewhere safe right away (1Password or similar). Then give it to the
Worker:

```bash
echo -n "$CLOCK_TOKEN" | npx wrangler secret put CLOCK_TOKEN
```

That takes effect **immediately** on the live Worker, so from now on the clock
gets `401` and shows NO DATA. Update the clock straight away:

1. Open the clock's web interface: its IP address in your browser, the same page
   where you set the Custom URL. The IP is in your router, or shown on the clock
   at boot.
2. Paste the new token in the **token** field. The URL stays
   `https://sugarclock-bridge.emve.workers.dev/clock`.
3. Save. The clock should show a value again within a minute.

Check it:

```bash
curl -s -o /dev/null -w "%{http_code}\n" -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/clock
curl -s -o /dev/null -w "%{http_code}\n" -H "Authorization: Bearer wrong" $BRIDGE/clock
```

The first should print `200` (or `503` if Nightscout happens to be stale); the
second, `401`.

If terminal A gets closed later, set both variables again: `export CLOCK_TOKEN='…'`
and `export BRIDGE=https://sugarclock-bridge.emve.workers.dev`.

## 3. Create the KV namespace

The Worker keeps its CareLink tokens and last snapshot here.

```bash
npx wrangler kv namespace create CARELINK
```

The output ends with a block like this:

```toml
[[kv_namespaces]]
binding = "CARELINK"
id = "3f2a9c…"
```

If wrangler asks whether to add it to your config for you, answer **no**:
`wrangler.toml` already has the block, and a second one would break the
config. Open `wrangler.toml` and replace `REPLACE_WITH_KV_NAMESPACE_ID` with
that id:

```toml
[[kv_namespaces]]
binding = "CARELINK"
id = "3f2a9c…"
```

## 4. Give the Worker write access to Nightscout

The Worker needs one of these two. **Option A** is simplest.

**A. The API secret.** ns10be calls it **API password** (server settings). It's
Nightscout's `API_SECRET`.

Check it first. This sends only its SHA-1 hash, just as the Worker will:

```bash
NS_PW='paste-the-api-password'
curl -s -H "api-secret: $(printf %s "$NS_PW" | shasum | cut -d' ' -f1)" \
  https://filipverheyen.10be.de/api/v1/verifyauth
```

You want `"canWrite":true` and `"isAdmin":true`. If you get
`"message":"UNAUTHORIZED"`, the password is wrong, or ns10be uses something
else; then use option B.

```bash
printf %s "$NS_PW" | npx wrangler secret put NS_API_SECRET
unset NS_PW
```

The Worker never sends the secret itself. It sends a SHA-1 hash in the
`api-secret` header, which is the standard Nightscout method.

**B. An admin token**, if you'd rather not hand over the API secret:
1. Open `https://filipverheyen.10be.de`, then the menu (☰) → **Admin Tools**.
2. Under **Subjects**, choose **Add new Subject**. Name it `sugarclock-bridge`,
   give it the role `admin`, and save.
3. Copy the access token it shows (`sugarclock-bridge-…`).
4. Then:
   ```bash
   npx wrangler secret put NS_UPLOAD_TOKEN
   # paste the token, Enter
   ```

The existing `NS_TOKEN` (follower) stays as it is. It's read-only and only
used for the fallback.

## 5. Prepare the CareLink login script

Do this before you turn anything off, so the gap in step 6 stays short. In
**terminal B**:

```bash
cd ~/Sites
git clone https://github.com/ondrej1024/carelink-python-client.git
cd carelink-python-client
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

The virtual environment (`.venv`) avoids the "externally-managed-environment"
error you got earlier.

If `pip install` fails on a dependency (selenium-wire doesn't like the newest
Python), use Python 3.12:

```bash
brew install python@3.12
rm -rf .venv
python3.12 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

**Switch the script from Firefox to Chrome.** It's one line, and the rest of
the script (selenium-wire, which reads the login redirect) works with Chrome too:

```bash
sed -i '' 's/webdriver.Firefox()/webdriver.Chrome()/' carelink_carepartner_api_login.py
grep -n "webdriver\." carelink_carepartner_api_login.py
# expect: driver = webdriver.Chrome()
```

The script still prints "opening Firefox instance…". That's only text; ignore
it.

On first run Selenium fetches a matching chromedriver itself, so it needs
internet. You don't need to install anything else.

Leave terminal B open, with `.venv` active.

## 6. Turn off ns10be's CareLink poller

In the ns10be dashboard, go to your server's plugins and **turn off the Medtronic
MMConnect plugin**: the one you set up with the dedicated account, on the same
page as `#mmconnect16715`. Save, and restart the server if ns10be asks you to.

From here on Nightscout gets no new readings until step 9.

Why it has to go: ns10be and the Worker would otherwise be two clients on one
CareLink login. That's the situation that broke things before, and the reason
ns10be got its own account.

## 7. Log in to CareLink once

In **terminal B** (`~/Sites/carelink-python-client`, `.venv` active):

```bash
python3 carelink_carepartner_api_login.py
```

1. A separate Chrome window opens on the CareLink login page. It's a clean,
   temporary profile, so your normal Chrome can stay open. The window says it's
   controlled by automated test software.
2. Log in with **ns10be's dedicated care-partner account**, not your own, and
   not Emilie's.
3. Solve the reCAPTCHA.
4. Wait. The script picks up the result itself, the window closes, and the
   script writes `logindata.json` to the current folder.

If Chrome shows "Your connection is not private" (selenium-wire intercepts
HTTPS with its own certificate), click anywhere on that page and type
`thisisunsafe`. That's Chrome's built-in bypass, and it only applies to this
temporary window.

If the window doesn't open or the script crashes with Chrome, the fallback is
Firefox: `brew install --cask firefox`, reverse the `sed` (swap Chrome and
Firefox), and run it again.

Check the result:

```bash
ls -l logindata.json
python3 -c "import json; print(sorted(json.load(open('logindata.json')).keys()))"
```

You should see at least `access_token`, `client_id` and `refresh_token`. The
EU login also adds `scope`; the older flow adds `client_secret` and
`mag-identifier`. Either version works.

**Don't run the Python client itself afterwards.** As soon as anything
refreshes the token, the copy in `logindata.json` is no longer valid.

## 8. Store the tokens in KV, then delete the file

In **terminal A** (the project folder, so wrangler uses the right `wrangler.toml`):

```bash
cd ~/Sites/pumpkin/bridge
npx wrangler kv key put tokens \
  --path ~/Sites/carelink-python-client/logindata.json \
  --binding CARELINK --remote
```

Check that the value arrived:

```bash
npx wrangler kv key list --binding CARELINK --remote
```

The list should show `tokens`. Then delete the file; it gives access to
Emilie's data:

```bash
rm ~/Sites/carelink-python-client/logindata.json
```

## 9. Deploy and test

```bash
cd ~/Sites/pumpkin/bridge
npx wrangler deploy
```

The output should list the `CARELINK` KV binding and `schedule: */30 * * * *`.

**Force a first CareLink fetch:**

```bash
curl -s -X POST -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/carelink/poll | python3 -m json.tool
```

Check these in the output:

| field | should be |
|---|---|
| `carelink.error` | `null` |
| `carelink.status` | `null` or `failures: 0` |
| `carelink.snapshot.last` | Emilie's current value and time |
| `carelink.snapshot.time_offset_h` | `0` (or a whole number; see troubleshooting) |
| `carelink.snapshot.iob` | the active insulin her pump shows |
| `carelink.snapshot.alerts` / `banners` | what the pump shows right now (often empty) |
| `carelink.nightscout_upload.ok` | `true`, with `entries` > 0 and `devicestatus: true` |
| `clock.source` | `carelink` |

**What the clock gets:**

```bash
curl -s -H "Authorization: Bearer $CLOCK_TOKEN" $BRIDGE/clock | python3 -m json.tool
```

**Check Nightscout:**

```bash
NS=https://filipverheyen.10be.de
NSTOKEN='follower-…'   # your NS_TOKEN
curl -s "$NS/api/v1/entries/sgv.json?count=3&token=$NSTOKEN" | python3 -m json.tool      # device: sugarclock-bridge
curl -s "$NS/api/v1/devicestatus.json?count=1&token=$NSTOKEN" | python3 -m json.tool     # pump.iob, reservoir, battery
curl -s "$NS/api/v2/properties/iob?token=$NSTOKEN" | python3 -m json.tool               # iob = pump value
```

In the Nightscout website you should see:
- the graph running up to now, with the gap from step 6 filled;
- the IOB pill matching the pump;
- a pump pill with reservoir and battery (enable `pump` under `SHOW_PLUGINS`
  if it doesn't show);
- after her next bolus, a treatment in the graph.

**Watch it live for a while:**

```bash
npx wrangler tail
```

Every 5 minutes or so you'll see a `/clock` request, and right after a new
reading appears in CareLink, a fetch followed by an upload.

## 10. Check again after an hour

- `/debug` → `carelink.status` should still be `null` or `failures: 0`.
- No double points in the Nightscout graph.
- Boluses and carbs appear correctly in Nightscout. This is the least-tested
  part: CareLink has used two record layouts, and I handle both. If something's
  missing, send me the `/debug` output.

---

## Day to day

**`bridge_alert: "CARELINK LOGIN NODIG"`** in `/clock`: CareLink rejected the
token, most likely because no refresh happened for more than a week. Redo
**steps 7 and 8**, then run the forced fetch from step 9. You don't need to
deploy again.

**`bridge_alert: "CARELINK STORING"`**: at least 3 failed fetches in a row,
usually CareLink itself. The Worker backs off (1, 2, 4 … 30 min) and recovers on
its own. Meanwhile the clock serves Nightscout, which isn't getting new data
either, so after 30 minutes the clock shows stale.

**Pausing the upload:** set `NS_UPLOAD = "false"` in `wrangler.toml` and deploy.
The clock keeps working.

**Going back to the old setup:**
1. Set `CARELINK_ENABLED = "false"` and `NS_UPLOAD = "false"` in `wrangler.toml`.
2. Deploy.
3. Turn ns10be's MMConnect plugin back on.

If you leave the Worker unused for a week, its token expires and you'll have to
log in again to switch back.

## Troubleshooting

| `carelink.error` / symptom | cause | fix |
|---|---|---|
| `no CareLink tokens in KV` | step 8 missing or written to the wrong namespace | `kv key list --binding CARELINK --remote`; redo step 8 from the project folder |
| `token refresh rejected (HTTP 4xx)` | refresh token used elsewhere, or older than a week | steps 7 and 8 again; make sure nothing else uses `logindata.json` |
| `no linked patient found` | the care-partner account can't see Emilie, or the name lookup fails | check the link in the CareLink app; or set `CARELINK_PATIENT_USERNAME` (her CareLink username) and `CARELINK_USERNAME` (the care-partner login) in `wrangler.toml` and deploy |
| `display/message -> HTTP 4xx/5xx` | CareLink API changed or is down | send me the `/debug` output |
| times an hour off (`time_offset_h` ≠ 0, reading looks 1–2 h old) | pump-local time without a zone | send me `/debug`; the offset correction may need a tweak |
| `nightscout_upload.error: … HTTP 401` | wrong `NS_API_SECRET`, or the token lacks the `admin` role | `npx wrangler secret put NS_API_SECRET` again (watch out for trailing spaces) |
| clock shows NO DATA, log shows HTTP 401 | the clock still has the old token, or a typo / trailing space | paste `$CLOCK_TOKEN` into the clock's token field again; test with the `curl` from step 2 |
| upload `ok` but nothing in Nightscout | Nightscout cache | refresh the site; check with the `curl` commands from step 9 |
| clock shows stale | no new reading anywhere for 30+ min | check the CareLink app on your phone; if that's current, look at `/debug` |

## Free-plan limits

- **Requests:** ~300 `/clock` calls plus 48 cron runs a day, against 100,000
  allowed.
- **KV writes:** ~300 snapshots plus ~30 token refreshes a day, against 1,000
  allowed.
- **Nightscout:** at most 4 lookups and 3 posts per new reading.
