#ifndef TAB_STRICT_H
#define TAB_STRICT_H

#include <windows.h>
#include <gdiplus.h>
#include <string>

// --- C++ Functions Declarations ---
void DrawStrictProtocolsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessStrictProtocolsMouseMove(float x, float y);
void ProcessStrictProtocolsMouseClick(float x, float y);

// --- HTML Code for Strict Protocols ---
const std::wstring HTML_STRICT_TAB = LR"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RasFocus Strict Protocols</title>
    <style>
        * { box-sizing: border-box; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
        body { background-color: #ffffff; color: #282828; margin: 0; padding: 20px; overflow-y: hidden; }
        h2 { color: #0ca8b0; font-size: 24px; margin-bottom: 5px; }
        p.subtitle { color: #787878; margin-top: 0; margin-bottom: 25px; }
        
        /* Top Controls Container */
        .top-controls { display: flex; gap: 20px; align-items: flex-end; margin-bottom: 30px; padding: 15px; background: #f8fafd; border-radius: 10px; border: 1px solid #e0e5ea; }
        .control-group { display: flex; flex-direction: column; gap: 5px; }
        label { font-weight: bold; font-size: 14px; }
        select { padding: 8px 12px; border: 1px solid #ccc; border-radius: 6px; font-size: 14px; outline: none; cursor: pointer; background: white; }
        
        .btn-start { padding: 10px 20px; background-color: #5aaaa14; background: #0ca8b0; color: white; border: none; border-radius: 6px; font-weight: bold; font-size: 15px; cursor: pointer; transition: 0.3s; margin-left: auto; min-width: 150px; }
        .btn-start:hover { background: #1eb9c3; }
        .btn-start.active { background: #e74c3c; }

        /* Security Toggles */
        .security-section { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 40px; }
        .toggle-item { display: flex; align-items: center; justify-content: space-between; padding: 12px 20px; background: #fff; border: 1px solid #e0e5ea; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.02); }
        .toggle-text strong { display: block; font-size: 15px; }
        .toggle-text span { font-size: 12px; color: #787878; }
        
        /* Switch CSS */
        .switch { position: relative; display: inline-block; width: 44px; height: 24px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
        .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .4s; border-radius: 50%; }
        input:checked + .slider { background-color: #0ca8b0; }
        input:checked + .slider:before { transform: translateX(20px); }
        input:disabled + .slider { opacity: 0.6; cursor: not-allowed; }

        /* Panic Button */
        .panic-container { display: flex; align-items: center; gap: 20px; padding-top: 20px; border-top: 1px solid #e0e5ea; }
        .btn-panic { padding: 12px 25px; background: #e74c3c; color: white; border: none; border-radius: 6px; font-weight: bold; font-size: 15px; cursor: pointer; transition: 0.3s; }
        .btn-panic:hover { background: #c0392b; }
        .analytics { font-weight: bold; color: #0ca8b0; font-size: 14px; }

        /* Modal / Overlay */
        .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); align-items: center; justify-content: center; }
        .modal-content { background: white; padding: 30px; border-radius: 12px; text-align: center; width: 350px; box-shadow: 0 10px 25px rgba(0,0,0,0.2); }
        .modal-content input { width: 100%; padding: 10px; margin: 15px 0; border: 1px solid #ccc; border-radius: 5px; font-size: 16px; }
        .modal-buttons { display: flex; gap: 10px; justify-content: center; }
        .btn-cancel { padding: 8px 15px; background: #eee; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; }
        .btn-confirm { padding: 8px 15px; background: #0ca8b0; color: white; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; }
    </style>
</head>
<body>

    <h2>Strict Protocols Engine</h2>
    <p class="subtitle">Advanced security measures to block distractions at the OS level.</p>

    <div class="top-controls">
        <div class="control-group">
            <label>Mode</label>
            <select id="selMode">
                <option value="0">Self Control</option>
                <option value="1">Friend Control</option>
            </select>
        </div>
        <div class="control-group">
            <label>Religion</label>
            <select id="selRel">
                <option value="0">Muslim</option><option value="1">Hindu</option>
                <option value="2">Christian</option><option value="3">Universal</option>
            </select>
        </div>
        <div class="control-group">
            <label>Language</label>
            <select id="selLang">
                <option value="0">Bangla</option><option value="1">English</option>
            </select>
        </div>
        <button id="btnStartFocus" class="btn-start">Start Focus</button>
    </div>

    <div class="security-section">
        <div class="toggle-item">
            <div class="toggle-text"><strong>Silent URL Tracking</strong><span>Tracks URLs from memory. Hard to bypass.</span></div>
            <label class="switch"><input type="checkbox" id="cbSilent"><span class="slider"></span></label>
        </div>
        <div class="toggle-item">
            <div class="toggle-text"><strong>Strict DNS/Hosts Block</strong><span>Blocks adult servers at the OS level.</span></div>
            <label class="switch"><input type="checkbox" id="cbDns"><span class="slider"></span></label>
        </div>
        <div class="toggle-item">
            <div class="toggle-text"><strong>Force SafeSearch</strong><span>Forces Google/YT into Strict Safe Mode.</span></div>
            <label class="switch"><input type="checkbox" id="cbSafe"><span class="slider"></span></label>
        </div>
        <div class="toggle-item">
            <div class="toggle-text"><strong>Block Incognito Tabs</strong><span>Closes Private/Incognito tabs instantly.</span></div>
            <label class="switch"><input type="checkbox" id="cbIncog"><span class="slider"></span></label>
        </div>
        <div class="toggle-item">
            <div class="toggle-text"><strong>Strict Mode (Anti-Bypass)</strong><span>Blocks Task Manager, RegEdit & Settings.</span></div>
            <label class="switch"><input type="checkbox" id="cbStrict"><span class="slider"></span></label>
        </div>
    </div>

    <div class="panic-container">
        <button id="btnPanic" class="btn-panic">PANIC LOCKDOWN (15m)</button>
        <span class="analytics" id="lblAnalytics">Analytics: Protected from 0 distractions.</span>
    </div>

    <div id="modalTime" class="modal">
        <div class="modal-content">
            <h3>Set Focus Duration</h3>
            <div style="display: flex; gap: 10px; margin: 20px 0; justify-content: center; align-items: center;">
                <input type="number" id="inpTime" value="60" style="width: 80px; text-align: center; margin: 0;"> <strong>Minutes</strong>
            </div>
            <div class="modal-buttons">
                <button class="btn-cancel" onclick="document.getElementById('modalTime').style.display='none'">Cancel</button>
                <button class="btn-confirm" id="btnConfirmTime">Start Focus</button>
            </div>
        </div>
    </div>

    <script>
        const webview = window.chrome.webview;
        let isFocusActive = false;

        // C++ থেকে ডেটা রিসিভ করা
        webview.addEventListener('message', event => {
            const data = event.data;
            if (data.type === "STATE_UPDATE") {
                document.getElementById('cbSilent').checked = data.cbSilentUrl;
                document.getElementById('cbDns').checked = data.cbDnsFilter;
                document.getElementById('cbSafe').checked = data.cbSafeSearch;
                document.getElementById('cbIncog').checked = data.cbIncognito;
                document.getElementById('cbStrict').checked = data.cbStrictMode;
                
                document.getElementById('selMode').value = data.mode;
                document.getElementById('selRel').value = data.rel;
                document.getElementById('selLang').value = data.lang;
                document.getElementById('lblAnalytics').innerText = "Analytics: Protected from " + data.blockedCount + " distractions.";

                isFocusActive = data.isFocus;
                const btnFocus = document.getElementById('btnStartFocus');
                
                if (isFocusActive) {
                    btnFocus.classList.add('active');
                    btnFocus.innerText = data.mode == 0 ? `Locked (${data.timeLeft}m)` : "Stop Focus";
                    // Disable toggles while active
                    ['cbSilent', 'cbDns', 'cbSafe', 'cbIncog', 'cbStrict', 'selMode', 'selRel', 'selLang'].forEach(id => document.getElementById(id).disabled = true);
                } else {
                    btnFocus.classList.remove('active');
                    btnFocus.innerText = "Start Focus";
                    ['cbSilent', 'cbDns', 'cbSafe', 'cbIncog', 'cbStrict', 'selMode', 'selRel', 'selLang'].forEach(id => document.getElementById(id).disabled = false);
                }

                if(data.isPanic) {
                    document.getElementById('btnPanic').innerText = "LOCKDOWN ACTIVE";
                    document.getElementById('btnPanic').style.background = "#7f8c8d";
                }
            }
        });

        // HTML লোড হওয়ার সাথে সাথে C++ এর কাছে ডেটা চাওয়া
        webview.postMessage("REQUEST_STATE");

        // টগল ও চেঞ্জ ইভেন্ট
        const sendAction = (action, val) => webview.postMessage(action + ":" + val);

        ['cbSilent', 'cbDns', 'cbSafe', 'cbIncog', 'cbStrict'].forEach(id => {
            document.getElementById(id).addEventListener('change', (e) => sendAction("TOGGLE", id + "_" + (e.target.checked ? "1" : "0")));
        });

        document.getElementById('selMode').addEventListener('change', (e) => sendAction("SET_MODE", e.target.value));
        document.getElementById('selRel').addEventListener('change', (e) => sendAction("SET_REL", e.target.value));
        document.getElementById('selLang').addEventListener('change', (e) => sendAction("SET_LANG", e.target.value));

        // Start Focus Button Logic
        document.getElementById('btnStartFocus').addEventListener('click', () => {
            if (isFocusActive) {
                if (document.getElementById('selMode').value == 1) {
                    let pass = prompt("Enter Friend's Password to stop:");
                    if(pass) sendAction("STOP_FOCUS", pass);
                }
            } else {
                if (document.getElementById('selMode').value == 0) {
                    document.getElementById('modalTime').style.display = 'flex';
                } else {
                    sendAction("START_FOCUS", "0"); // Friend mode doesn't need time
                }
            }
        });

        document.getElementById('btnConfirmTime').addEventListener('click', () => {
            let mins = document.getElementById('inpTime').value;
            sendAction("START_FOCUS", mins);
            document.getElementById('modalTime').style.display = 'none';
        });

        document.getElementById('btnPanic').addEventListener('click', () => {
            if(confirm("Are you sure you want to lockdown internet browsers for 15 minutes?")) {
                sendAction("START_PANIC", "1");
            }
        });
    </script>
</body>
</html>
)html";

#endif // TAB_STRICT_H
