const char ph_html[] = R"rawliteral(
HTTP/1.1 200 OK
Content-Type: text/html
Connection: close

<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Mode Toggle TEST</title>
  <style>
    /* Prevent Mobile Safari from auto‑resizing text */
    html {
      -webkit-text-size-adjust: 100%;
      -ms-text-size-adjust: 100%;
    }
    body {
      position: relative;
      text-align: center;
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 0;
    }
    /* i icon in top‑right */
    #info-icon {
      position: absolute;
      top: 0px;
      left: 50%;
      transform: translate(170px, -40px);
      font-size: 2.5rem;
      font-weight: 1000;
      cursor: pointer;
      user-select: none;
    }
    /* when info mode is active */
    body.show-info .number-input,
    body.show-info .unit {
      display: none;
    }
    body.show-info .info-text {
      display: inline-block;
      margin-left: 10px;
      font-size: 1.5rem;
      font-weight: 1000;
      color: #333;
      text-align: left;
    }

    .mode {
      margin-top: 70px;
      display: flex;
      flex-direction: row;
      justify-content: center;
      align-items: center;
      gap: 10px;
      cursor: pointer;
    }
    .mode span {
      transition: all 0.3s ease;
    }
    .active {
      font-size: 4.5rem;
      font-weight: bold;
    }
    .inactive {
      font-size: 0.8rem;
      font-weight: 300;
    }
    .item {
      display: flex;
      justify-content: center;
      align-items: center;
      margin: 10px 0;
      font-size: 1.0rem;
    }
    #icon {
      margin-right: 10px;
      font-size: 5.5rem;
    }
    .pump-button {
      background: none;
      border: none;
      padding: 0;
      margin-right: 10px;
      font-size: 5.5rem;
      cursor: pointer;
      user-select: none;
    }
    .number-input {
      /* Force the same look everywhere */
      -webkit-appearance: none;
      -moz-appearance: none;
      appearance: none;
      border: 0;
      border-radius: 4px;
      text-align: center;
      font-size: 6rem;
      line-height: 1;        /* normalize line‑height */
      height: 6.0rem;        /* fix height to match font-size */
      width: 12.5rem;
      padding: 0;            /* remove any default padding */
      margin: 0;             /* remove any default margin */
      outline: none;
    }
    .unit {
      font-size: 1.5rem;
      transform: translate(0px, 20px);
    }
    .info-text {
      display: none;
    }
  </style>
</head>
<body>
  <!-- i icon trigger -->
  <span id="info-icon">i</span>

  <div class="mode" onclick="toggleMode()">
    <span id="auto" class="active">AUTO</span>
    <span id="manual" class="inactive">MANUAL</span>
  </div>
  <br/><br/>

  <div class="item">
    <span id="icon">🔬</span>
    <span class="info-text">Current ph<br>Tripple Tap to &emsp; &emsp;<br>calibrate</span>
    <input
      id="ph"
      class="number-input"
      type="text"
      inputmode="numeric"
      pattern="[0-9]*"
      data-z="2"
      data-v="1"
      readonly
    />
    <span class="unit">ph</span>
  </div>

  <div class="item">
    <span id="icon">⛽</span>
    <span class="info-text">Tank Level&nbsp;<br>Tripple Tap to&emsp;&emsp;<br>set fill level</span>
    <input
      id="liters"
      class="number-input"
      type="text"
      inputmode="numeric"
      pattern="[0-9]*"
      data-z="1"
      data-v="2"
      readonly
    />
    <span class="unit">L</span>
  </div>

  <div class="item">
    <button id="pump-button" class="pump-button">🔫</button>
    <span class="info-text">ml to pump&emsp;&emsp;&emsp;&nbsp;&nbsp;<br> </span>
    <input
      id="ml"
      class="number-input"
      type="text"
      inputmode="numeric"
      pattern="[0-9]*"
      data-z="2"
      data-v="1"
      readonly
    />
    <span class="unit">ml</span>
  </div>

  <script>
    function formatValue(n, z, v) {
      const maxDigits = z + v;
      const s = String(n).padStart(maxDigits, '0');
      return v > 0 ? s.slice(0, z) + '.' + s.slice(z) : s.slice(0, z);
    }

    function toggleMode() {
      const auto = document.getElementById('auto');
      const manual = document.getElementById('manual');
      if (auto.classList.contains('active')) {
        auto.className = 'inactive';
        manual.className = 'active';
      } else {
        auto.className = 'active';
        manual.className = 'inactive';
      }
    }

    document.addEventListener('DOMContentLoaded', () => {
      const auto = document.getElementById('auto');
      const mlInput = document.getElementById('ml');
      const pumpBtn = document.getElementById('pump-button');
      const inputs = Array.from(document.querySelectorAll('.number-input'));

      let mlAutoRaw = 0;
      let pumpMlRaw = 0;

      /* --- Arduino connectivity state --- */
      let pollTimer = null;
      let pollingPaused = false;     // true while user is editing any field
      let fetching = false;          // prevent overlapping fetches
      const POLL_INTERVAL = 1000;    // ms
      const endpoints = {
        auto: { ph: '/phsetpoint', liters: '/tankL', ml: '/Automl' },
        manual: { ph: '/phsetpoint', liters: '/tankL', ml: '/Manualml' }
      };
  // Track last value (raw) we actually transmitted per field to avoid duplicates
  const lastSent = { ph: null, liters: null, ml: null };

      function currentModeKey() { return auto.classList.contains('active') ? 'auto' : 'manual'; }

      function pausePolling() { pollingPaused = true; }
      function resumePolling() { pollingPaused = false; }
      function startPolling() {
        if (pollTimer) return;
        pollTimer = setInterval(fetchStats, POLL_INTERVAL);
      }
      function stopPolling() { if (pollTimer) { clearInterval(pollTimer); pollTimer = null; } }

      async function fetchModeAndStart() {
        try {
          const r = await fetch('/mode', { cache: 'no-store' });
          if (!r.ok) throw new Error('Mode HTTP ' + r.status);
          const text = (await r.text()).trim();
          const isAuto = text === '1';
          if (isAuto && !auto.classList.contains('active')) {
            // switch to AUTO
            auto.className = 'active';
            document.getElementById('manual').className = 'inactive';
          } else if (!isAuto && auto.classList.contains('active')) {
            // switch to MANUAL
            auto.className = 'inactive';
            document.getElementById('manual').className = 'active';
          }
          updateInputs();
          updatePumpBtnState();
          startPolling();
          fetchStats(); // immediate first fetch
        } catch (e) {
          console.warn('Failed to fetch /mode:', e);
          // Still start polling in case endpoint temporarily unavailable
          startPolling();
        }
      }

      function rawFromFloat(floatVal, v, maxRaw) {
        if (isNaN(floatVal)) return 0;
        let raw = Math.round(floatVal * (10 ** v));
        if (raw < 0) raw = 0;
        if (raw > maxRaw) raw = maxRaw;
        return raw;
      }

      async function fetchStats() {
        if (pollingPaused || fetching) return;
        fetching = true;
        const modeKey = currentModeKey();
        const url = modeKey === 'auto' ? '/AutoStats' : '/ManualStats';
        try {
          const r = await fetch(url, { cache: 'no-store' });
          if (!r.ok) throw new Error('Stats HTTP ' + r.status);
          const data = await r.json();
          applyStats(data, modeKey);
        } catch (e) {
          console.warn('Failed to fetch stats:', e);
        } finally {
          fetching = false;
        }
      }

      function applyStats(data, modeKey) {
        // Update inputs only if user not editing them
        ['ph','liters','ml'].forEach(id => {
          const el = document.getElementById(id);
          if (!el || el.dataset.editing === 'true') return; // skip active edit
          const z = parseInt(el.dataset.z, 10) || 0;
            const v = parseInt(el.dataset.v, 10) || 0;
            const maxDigits = z + v;
            const maxRaw = 10 ** maxDigits - 1;
          let floatVal;
          if (id === 'ph') floatVal = data.ph;
          else if (id === 'liters') floatVal = data.tankL;
          else floatVal = data.ml;
          const newRaw = rawFromFloat(parseFloat(floatVal), v, maxRaw);
          if (typeof el._setRaw === 'function') {
            el._setRaw(newRaw, modeKey);
          }
        });
        // Pump state
        if (typeof data.pumpactive !== 'undefined') {
          const active = parseInt(data.pumpactive, 10) === 1;
          if (active) {
            pumpBtn.style.filter = 'none';
          } else {
            pumpBtn.style.filter = 'grayscale(100%)';
          }
        }
      }

      function sendFieldValue(fieldId, raw, z, v) {
        if (!raw) return; // skip zero
        // De-duplicate: if same raw value already sent for this field while in current mode, skip
        if (lastSent[fieldId] === raw) return;
        const modeKey = currentModeKey();
        const endpointMap = endpoints[modeKey];
        const ep = endpointMap[fieldId];
        if (!ep) return;
        const floatVal = raw / (10 ** v);
        // Assumption: Arduino expects value query param, e.g. /phsetpoint?value=7.2
        lastSent[fieldId] = raw;
        fetch(`${ep}?value=${encodeURIComponent(floatVal)}`)
          .then(()=>{
            // Optional debug log; comment out if not needed
            // console.log('Sent', fieldId, 'raw', raw, 'float', floatVal);
          })
          .catch(err => console.warn('Send failed', ep, err));
      }

      function updateInputs() {
        const isAuto = auto.classList.contains('active');
        inputs.forEach(input => {
          const holdRequired = !(input.id === 'ml' && !isAuto);
          input.dataset.holdRequired = holdRequired;
          input.readOnly = holdRequired;
        });
      }

      function updatePumpBtnState() {
        const isAuto = auto.classList.contains('active');
        if (isAuto) {
          pumpBtn.disabled = true;
          pumpBtn.style.filter = 'none';
          mlInput.value = formatValue(mlAutoRaw, 2, 1);
        } else {
          pumpBtn.disabled = false;
          pumpBtn.style.filter = 'grayscale(100%)';
          pumpMlRaw = 0;
          mlInput.value = formatValue(pumpMlRaw, 2, 1);
        }
      }

      // Info-icon click event to toggle info text
      const infoIcon = document.getElementById('info-icon');
      infoIcon.addEventListener('click', (e) => {
        e.stopPropagation();
        document.body.classList.toggle('show-info');
      });

      document.addEventListener('click', (e) => {
        if (!e.target.matches('#info-icon') && document.body.classList.contains('show-info')) {
          document.body.classList.remove('show-info');
        }
        if (!e.target.matches('.number-input')) {
          inputs.forEach((input) => input.blur());
        }
      });

      inputs.forEach(input => {
        const z = Math.max(0, parseInt(input.dataset.z, 10) || 0);
        const v = Math.max(0, parseInt(input.dataset.v, 10) || 0);
        const maxDigits = z + v;
        const maxRaw = 10 ** maxDigits - 1;

        let raw = 0;
        let originalRaw = raw;
        let tapCount = 0, tapTimer;

        function updateDisplay() {
          input.value = formatValue(raw, z, v);
          const L = input.value.length;
          input.setSelectionRange(L, L);
        }
        updateDisplay();

        // expose setter for polling updates
        input._setRaw = function(newRaw, modeKey) {
          raw = newRaw;
          if (input.id === 'ml') {
            if ((modeKey || currentModeKey()) === 'auto') mlAutoRaw = raw; else pumpMlRaw = raw;
          }
          updateDisplay();
        };

        input.addEventListener('keydown', e => {
          if (input.readOnly) { e.preventDefault(); return; }
          const k = e.key;
          if (/\d/.test(k)) {
            e.preventDefault();
            raw = raw * 10 + Number(k);
            if (raw > maxRaw) raw = Math.floor(raw / 10);
            updateDisplay();
            input.dataset.dirty = 'true';
          } else if (k === 'Backspace') {
            e.preventDefault();
            raw = Math.floor(raw / 10);
            updateDisplay();
            input.dataset.dirty = 'true';
          } else {
            e.preventDefault();
          }
        });

        input.addEventListener('focus', () => {
          if (!input.readOnly) {
            input.dataset.editing = 'true';
            if (!input.dataset.dirty) input.dataset.dirty = 'false';
            pausePolling();
          }
        });

        ['mouseup','touchend'].forEach(evt => {
          input.addEventListener(evt, e => {
            if (input.id === 'ml' && input.dataset.holdRequired === 'false') {
              e.preventDefault();
              originalRaw = raw;
              raw = 0;
              updateDisplay();
              input.focus();
              // mark editing & pause polling (manual ml single tap edit)
              input.dataset.editing = 'true';
              input.dataset.dirty = 'false';
              pausePolling();
              return;
            }
            if (input.dataset.holdRequired === 'true') {
              e.preventDefault();
              tapCount++;
              if (tapCount === 1) {
                originalRaw = raw;
                tapTimer = setTimeout(() => tapCount = 0, 600);
              }
              if (tapCount === 3) {
                clearTimeout(tapTimer);
                tapCount = 0;
                raw = 0;
                updateDisplay();
                input.readOnly = false;
                input.focus();
                // mark editing & pause polling (after triple tap unlock)
                input.dataset.editing = 'true';
                input.dataset.dirty = 'false';
                pausePolling();
              }
            }
          });
        });

        // Ensure polling pauses on first key entry if it somehow wasn't paused yet
        input.addEventListener('keydown', () => {
          if (input.readOnly) return;
            if (input.dataset.editing !== 'true') {
              input.dataset.editing = 'true';
              pausePolling();
            }
        }, { once: false });

        input.addEventListener('blur', () => {
          if (raw === 0) {
            if (input.id === 'ml') {
              if (auto.classList.contains('active')) {
                raw = mlAutoRaw;
              } else {
                raw = pumpMlRaw;
              }
            } else {
              raw = originalRaw;
            }
            updateDisplay();
          } else if (input.id === 'ml') {
            if (auto.classList.contains('active')) {
              mlAutoRaw = raw;
            } else {
              pumpMlRaw = raw;
            }
          }
          if (input.dataset.holdRequired === 'true') {
            input.readOnly = true;
          }
            // send only if user actually entered digits (dirty) and value non-zero now
            if (input.dataset.dirty === 'true' && raw !== 0) {
              sendFieldValue(input.id === 'liters' ? 'liters' : input.id, raw, z, v);
            }
          input.dataset.editing = 'false';
            input.dataset.dirty = 'false';
          resumePolling();
        });
      });

      pumpBtn.addEventListener('click', () => {
        if (!pumpBtn.disabled) {
          pumpBtn.style.filter =
            pumpBtn.style.filter === 'grayscale(100%)'
              ? 'none'
              : 'grayscale(100%)';
          // send pump state only in manual mode
          if (!auto.classList.contains('active')) {
            const active = pumpBtn.style.filter !== 'grayscale(100%)';
            fetch(active ? '/pumpactive' : '/pumpinactive').catch(err => console.warn('Pump toggle send failed', err));
          }
        }
      });

      const origToggle = window.toggleMode;
      window.toggleMode = () => {
        origToggle();
        updateInputs();
        updatePumpBtnState();
        // Mode switch: clear ml editing state; resume polling
        resumePolling();
      };

      updateInputs();
      updatePumpBtnState();
      fetchModeAndStart(); // initial mode + start polling
    });
  </script>
</body>
</html>
)rawliteral";