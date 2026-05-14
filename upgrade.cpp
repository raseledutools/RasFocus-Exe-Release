<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RasFocus+ — Choose Your Plan</title>
    <link href="https://fonts.googleapis.com/css2?family=Sora:wght@300;400;500;600;700;800&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">
    <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.0/css/all.min.css" rel="stylesheet">
    <style>
        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

        :root {
            --teal: #00b09b;
            --teal-dark: #007a6e;
            --teal-light: #e0faf7;
            --pink: #e9184c;
            --pink-soft: #fff0f4;
            --gold: #f5b800;
            --gold-soft: #fffbea;
            --bg: #f5fffe;
            --card: #ffffff;
            --border: #d4f0ed;
            --text: #1a2e2d;
            --muted: #6b8b8a;
            --radius: 20px;
            --shadow: 0 4px 30px rgba(0,176,155,0.10);
            --shadow-lg: 0 12px 48px rgba(0,176,155,0.18);
        }

        /* ── HOME NAV ── */
        .top-nav-bar {
            display: flex;
            align-items: center;
            justify-content: space-between;
            max-width: 980px;
            margin: 0 auto 24px;
            padding: 0 4px;
        }
        .home-btn {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            background: white;
            border: 1.5px solid var(--border);
            border-radius: 99px;
            padding: 8px 18px;
            font-family: 'Sora', sans-serif;
            font-size: 13px;
            font-weight: 700;
            color: var(--teal-dark);
            text-decoration: none;
            cursor: pointer;
            box-shadow: 0 2px 12px rgba(0,176,155,0.10);
            transition: all 0.2s ease;
        }
        .home-btn:hover {
            background: var(--teal-light);
            border-color: var(--teal);
            transform: translateX(-2px);
            box-shadow: 0 4px 16px rgba(0,176,155,0.18);
        }
        .home-btn i { font-size: 12px; }

        body {
            font-family: 'Sora', sans-serif;
            background: var(--bg);
            color: var(--text);
            min-height: 100vh;
            padding: 32px 16px 60px;
            background-image:
                radial-gradient(ellipse 700px 400px at 80% -10%, rgba(0,176,155,0.10) 0%, transparent 70%),
                radial-gradient(ellipse 400px 300px at 10% 90%, rgba(0,176,155,0.07) 0%, transparent 70%);
        }

        /* ── HEADER ── */
        .header {
            text-align: center;
            margin-bottom: 44px;
            animation: fadeUp 0.6s ease both;
        }
        .logo-badge {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            background: white;
            border: 1.5px solid var(--border);
            border-radius: 99px;
            padding: 6px 18px;
            font-size: 13px;
            font-weight: 700;
            color: var(--teal-dark);
            margin-bottom: 20px;
            box-shadow: 0 2px 12px rgba(0,176,155,0.10);
            letter-spacing: 0.03em;
        }
        .logo-dot { width: 8px; height: 8px; background: var(--teal); border-radius: 50%; }
        h1 {
            font-size: clamp(26px, 5vw, 42px);
            font-weight: 800;
            color: var(--text);
            line-height: 1.15;
            margin-bottom: 10px;
        }
        h1 span { color: var(--teal); }
        .subtitle {
            font-size: 15px;
            color: var(--muted);
            max-width: 480px;
            margin: 0 auto 18px;
            line-height: 1.6;
        }
        .uid-pill {
            display: inline-flex;
            align-items: center;
            gap: 7px;
            background: white;
            border: 1.5px solid var(--border);
            border-radius: 99px;
            padding: 6px 16px;
            font-family: 'JetBrains Mono', monospace;
            font-size: 12px;
            color: var(--teal-dark);
            font-weight: 600;
            box-shadow: 0 2px 10px rgba(0,176,155,0.08);
        }
        .uid-pill i { color: var(--teal); font-size: 11px; }

        /* ── PRICING GRID ── */
        .pricing-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
            max-width: 980px;
            margin: 0 auto;
        }

        .plan-card {
            background: var(--card);
            border: 1.5px solid var(--border);
            border-radius: var(--radius);
            padding: 28px 24px;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
            box-shadow: var(--shadow);
            transition: transform 0.22s ease, box-shadow 0.22s ease;
            animation: fadeUp 0.7s ease both;
            position: relative;
            overflow: visible;
        }
        .plan-card:hover { transform: translateY(-6px); box-shadow: var(--shadow-lg); }
        .plan-card.featured {
            border-color: var(--teal);
            border-width: 2px;
            background: linear-gradient(160deg, #ffffff 60%, #e8faf8 100%);
        }
        .best-badge {
            position: absolute;
            top: -14px;
            left: 50%;
            transform: translateX(-50%);
            background: linear-gradient(90deg, var(--teal), var(--teal-dark));
            color: white;
            font-size: 10.5px;
            font-weight: 700;
            letter-spacing: 0.1em;
            padding: 4px 16px;
            border-radius: 99px;
            white-space: nowrap;
            box-shadow: 0 4px 12px rgba(0,176,155,0.35);
        }

        .plan-icon {
            width: 50px; height: 50px;
            border-radius: 14px;
            display: flex; align-items: center; justify-content: center;
            font-size: 22px;
            margin-bottom: 14px;
        }
        .plan-name {
            font-size: 19px;
            font-weight: 700;
            color: var(--text);
            margin-bottom: 4px;
        }
        .plan-sub {
            font-size: 12px;
            color: var(--muted);
            margin-bottom: 18px;
        }
        .price-block {
            border-radius: 14px;
            padding: 14px 18px;
            margin-bottom: 20px;
            display: flex;
            align-items: baseline;
            gap: 6px;
        }
        .price-big {
            font-size: 32px;
            font-weight: 800;
            line-height: 1;
        }
        .price-period { font-size: 13px; color: var(--muted); }

        /* radio sub-options */
        .sub-options { display: flex; flex-direction: column; gap: 10px; margin-bottom: 18px; }
        .sub-option {
            display: flex;
            align-items: center;
            justify-content: space-between;
            border: 1.5px solid #e5eeed;
            border-radius: 12px;
            padding: 11px 14px;
            cursor: pointer;
            transition: border-color 0.2s, background 0.2s;
        }
        .sub-option:has(input:checked) {
            border-color: var(--teal);
            background: var(--teal-light);
        }
        .sub-option input { accent-color: var(--teal); width: 16px; height: 16px; }
        .sub-option-label { margin-left: 10px; }
        .sub-option-name { font-size: 13px; font-weight: 700; color: var(--text); }
        .sub-option-note { font-size: 11px; color: var(--muted); }
        .sub-option-price { font-size: 15px; font-weight: 800; color: var(--teal-dark); }

        .feature-list { list-style: none; margin-bottom: 22px; display: flex; flex-direction: column; gap: 9px; }
        .feature-list li { display: flex; align-items: center; gap: 9px; font-size: 12.5px; color: #3a5554; }
        .feature-list li i { color: var(--teal); font-size: 13px; flex-shrink: 0; }
        .feature-list li i.fa-star { color: var(--gold); }

        /* ── BUTTONS ── */
        .btn {
            width: 100%;
            padding: 13px 20px;
            border-radius: 12px;
            border: none;
            cursor: pointer;
            font-family: 'Sora', sans-serif;
            font-weight: 700;
            font-size: 14px;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
            transition: all 0.2s ease;
        }
        .btn-primary { background: linear-gradient(135deg, var(--teal), var(--teal-dark)); color: white; box-shadow: 0 4px 16px rgba(0,176,155,0.30); }
        .btn-primary:hover { transform: translateY(-2px); box-shadow: 0 8px 24px rgba(0,176,155,0.40); }
        .btn-pink { background: linear-gradient(135deg, #ff3366, #e9184c); color: white; box-shadow: 0 4px 16px rgba(233,24,76,0.28); }
        .btn-pink:hover { transform: translateY(-2px); box-shadow: 0 8px 24px rgba(233,24,76,0.38); }
        .btn-ghost { background: #f0f4f3; color: #5a7a79; }
        .btn-ghost:hover { background: #e0eeec; }

        /* ── PAYMENT SECTION ── */
        #payment-section {
            max-width: 560px;
            margin: 0 auto;
            background: white;
            border-radius: 24px;
            border: 1.5px solid var(--border);
            box-shadow: var(--shadow-lg);
            overflow: hidden;
            animation: fadeUp 0.5s ease both;
        }
        .pay-header {
            background: linear-gradient(135deg, var(--teal), var(--teal-dark));
            padding: 28px 28px 24px;
            color: white;
            position: relative;
        }
        .pay-back {
            display: inline-flex; align-items: center; gap: 6px;
            font-size: 13px; font-weight: 600;
            color: rgba(255,255,255,0.85);
            background: rgba(255,255,255,0.15);
            border: none; border-radius: 99px;
            padding: 6px 14px; cursor: pointer;
            margin-bottom: 16px;
            transition: background 0.2s;
        }
        .pay-back:hover { background: rgba(255,255,255,0.25); }
        .pay-title { font-size: 22px; font-weight: 800; margin-bottom: 4px; }
        .pay-subtitle { font-size: 13px; opacity: 0.85; }
        .pay-summary-pill {
            display: inline-flex; align-items: center; gap: 8px;
            background: rgba(255,255,255,0.18);
            border-radius: 99px;
            padding: 7px 16px;
            margin-top: 14px;
            font-size: 13px; font-weight: 700;
        }
        .pay-summary-pill .amount { font-size: 18px; font-weight: 800; }

        .pay-body { padding: 26px 28px; }
        .pay-section-title {
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.12em;
            text-transform: uppercase;
            color: var(--muted);
            margin-bottom: 12px;
        }

        /* ── PAYMENT METHOD BUTTONS ── */
        .methods-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 12px;
            margin-bottom: 22px;
        }
        .method-btn {
            background: white;
            border: 1.5px solid #e8efee;
            border-radius: 14px;
            padding: 14px 10px 12px;
            cursor: pointer;
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 8px;
            transition: all 0.2s ease;
            position: relative;
            font-family: 'Sora', sans-serif;
        }
        .method-btn:hover:not(.disabled) {
            border-color: var(--teal);
            box-shadow: 0 4px 16px rgba(0,176,155,0.14);
            transform: translateY(-2px);
        }
        .method-btn.disabled { opacity: 0.45; cursor: not-allowed; }
        .method-btn .coming-soon {
            position: absolute;
            top: -8px; left: 50%; transform: translateX(-50%);
            background: #fbbf24;
            color: #78350f;
            font-size: 9px; font-weight: 700;
            padding: 2px 8px; border-radius: 99px;
            white-space: nowrap;
        }
        .method-btn .method-name {
            font-size: 11px; font-weight: 700; color: var(--text);
            text-align: center;
        }

        /* Real logo images */
        .pay-logo {
            width: auto;
            height: 38px;
            max-width: 82px;
            object-fit: contain;
            border-radius: 6px;
        }

        /* LIVE green badge on bKash */
        .live-badge {
            position: absolute;
            top: -8px; right: -6px;
            background: linear-gradient(135deg, #00c48c, #00a374);
            color: white;
            font-size: 8.5px; font-weight: 800;
            letter-spacing: 0.08em;
            padding: 2px 7px;
            border-radius: 99px;
            box-shadow: 0 2px 6px rgba(0,196,140,0.40);
        }

        .divider {
            height: 1px;
            background: #eef3f2;
            margin: 18px 0;
        }

        .security-note {
            display: flex;
            align-items: center;
            gap: 8px;
            background: #f0fdf9;
            border: 1px solid #c3ede7;
            border-radius: 10px;
            padding: 12px 14px;
            font-size: 12px;
            color: var(--teal-dark);
            margin-top: 4px;
        }
        .security-note i { font-size: 16px; color: var(--teal); flex-shrink: 0; }

        /* HIDDEN */
        .hidden { display: none !important; }

        /* SPINNER OVERLAY */
        .spinner-overlay {
            position: fixed; inset: 0;
            background: rgba(255,255,255,0.85);
            backdrop-filter: blur(6px);
            display: flex; flex-direction: column;
            align-items: center; justify-content: center;
            z-index: 999;
        }
        .spinner-ring {
            width: 52px; height: 52px;
            border: 4px solid var(--teal-light);
            border-top-color: var(--teal);
            border-radius: 50%;
            animation: spin 0.75s linear infinite;
            margin-bottom: 16px;
        }
        .spinner-text { font-size: 15px; font-weight: 600; color: var(--teal-dark); }

        @keyframes spin { to { transform: rotate(360deg); } }
        @keyframes fadeUp {
            from { opacity: 0; transform: translateY(18px); }
            to { opacity: 1; transform: translateY(0); }
        }
    </style>
</head>
<body>

<!-- SPINNER -->
<div class="spinner-overlay hidden" id="spinner">
    <div class="spinner-ring"></div>
    <div class="spinner-text">Connecting to bKash…</div>
    <div style="font-size:13px; color:#6b8b8a; margin-top:6px;">Please wait a few seconds ⏳</div>
</div>

<!-- HOME NAV BAR -->
<div class="top-nav-bar">
    <a href="index.html" class="home-btn">
        <i class="fas fa-arrow-left"></i> Back to Home
    </a>
    <a href="index.html" class="home-btn" style="gap:6px;">
        <i class="fas fa-house"></i> RasFocus+
    </a>
</div>

<div class="header">
    <div class="logo-badge"><div class="logo-dot"></div> RasFocus<span style="color:var(--teal)">+</span></div>
    <h1>Choose Your <span>Focus Plan</span></h1>
    <p class="subtitle">Block distractions, unlock productivity, and stay on track — on your terms.</p>
    <div class="uid-pill">
        <i class="fas fa-fingerprint"></i>
        UID: <span id="uid-display" style="font-weight:800">Loading…</span>
    </div>
</div>

<!-- ═══════════════ PRICING SECTION ═══════════════ -->
<div id="pricing-section" class="pricing-grid">

    <!-- FREE / TRIAL -->
    <div class="plan-card" style="animation-delay:0s">
        <div>
            <div class="plan-icon" style="background:#f0f9ff; color:#0ea5e9;"><i class="fas fa-hourglass-half"></i></div>
            <div class="plan-name">14-Day Free Trial</div>
            <div class="plan-sub">Transitions automatically to Free Basic</div>
            <div class="price-block" style="background:#f8fafc;">
                <div class="price-big" style="color:#1a2e2d;">৳ 0</div>
                <div class="price-period">then Free Basic</div>
            </div>
            <ul class="feature-list">
                <li><i class="fas fa-check-circle"></i> 100% Premium features for 14 days</li>
                <li><i class="fas fa-arrow-right" style="color:#94a3b8"></i> Auto-switches to Free Basic</li>
                <li><i class="fas fa-minus-circle" style="color:#f59e0b"></i> Standard block rules only after trial</li>
            </ul>
        </div>
        <button class="btn btn-ghost" onclick="selectFreeTrial()">Current Default Plan</button>
    </div>

    <!-- PREMIUM PRO -->
    <div class="plan-card" style="animation-delay:0.1s">
        <div>
            <div class="plan-icon" style="background:#fff0f4; color:#e9184c;"><i class="fas fa-bolt"></i></div>
            <div class="plan-name">Premium Pro</div>
            <div class="plan-sub">Flexible billing, full power</div>

            <div class="sub-options">
                <label class="sub-option">
                    <input type="radio" name="premium_plan" value="monthly" checked>
                    <div class="sub-option-label">
                        <div class="sub-option-name">1 Month Access</div>
                        <div class="sub-option-note">Renew every month</div>
                    </div>
                    <div class="sub-option-price">৳ 50</div>
                </label>
                <label class="sub-option">
                    <input type="radio" name="premium_plan" value="6month">
                    <div class="sub-option-label">
                        <div class="sub-option-name">6 Months Saver</div>
                        <div class="sub-option-note">Save ৳ 0 — semi-annual billing</div>
                    </div>
                    <div class="sub-option-price">৳ 300</div>
                </label>
            </div>

            <ul class="feature-list">
                <li><i class="fas fa-check-circle"></i> Strict Block Protocols</li>
                <li><i class="fas fa-check-circle"></i> Real-time Cloud Sync & Backup</li>
                <li><i class="fas fa-check-circle"></i> Halal Guard Filter Engine</li>
            </ul>
        </div>
        <button class="btn btn-pink" onclick="showPaymentMethods('flexible')">
            <i class="fas fa-lock" style="font-size:12px"></i> Choose & Pay
        </button>
    </div>

    <!-- 1 YEAR ULTIMATE -->
    <div class="plan-card featured" style="animation-delay:0.2s">
        <div class="best-badge">★ BEST VALUE</div>
        <div>
            <div class="plan-icon" style="background:#fffbea; color:#d97706;"><i class="fas fa-crown"></i></div>
            <div class="plan-name">1 Year Ultimate</div>
            <div class="plan-sub">Maximum savings &amp; unlimited focus</div>
            <div class="price-block" style="background:linear-gradient(135deg, #00b09b, #007a6e);">
                <div class="price-big" style="color:white;">৳ 500</div>
                <div class="price-period" style="color:rgba(255,255,255,0.75);">/ 365 days</div>
            </div>
            <ul class="feature-list">
                <li><i class="fas fa-star"></i> All Premium Pro Features</li>
                <li><i class="fas fa-check-circle"></i> Priority Developer Support</li>
                <li><i class="fas fa-check-circle"></i> No Recurring Billing Stress</li>
                <li><i class="fas fa-check-circle"></i> Early Access to New Features</li>
            </ul>
        </div>
        <button class="btn btn-primary" onclick="showPaymentMethods('yearly')">
            <i class="fas fa-lock" style="font-size:12px"></i> Choose & Pay
        </button>
    </div>
</div>

<!-- ═══════════════ PAYMENT SECTION ═══════════════ -->
<div id="payment-section" class="hidden">
    <div class="pay-header">
        <button class="pay-back" onclick="goBackToPricing()">
            <i class="fas fa-arrow-left"></i> Back to Plans
        </button>
        <div class="pay-title">Secure Checkout</div>
        <div class="pay-subtitle">RasFocus+ — Powered by tokenized payment APIs</div>
        <div class="pay-summary-pill">
            <i class="fas fa-receipt" style="font-size:13px; opacity:0.8"></i>
            <span id="summary-plan-name">—</span>
            &nbsp;·&nbsp;
            <span class="amount" id="summary-amount">৳ —</span>
        </div>

        <!-- UID shown on payment page -->
        <div style="margin-top:12px; font-size:11px; opacity:0.75; font-family:'JetBrains Mono',monospace; display:flex; align-items:center; gap:6px;">
            <i class="fas fa-fingerprint"></i>
            Account UID: <strong id="pay-uid" style="font-size:12px; opacity:1;">—</strong>
        </div>
    </div>

    <div class="pay-body">

        <!-- Error / Success Notice Area -->
        <div id="pay-notice" style="display:none; align-items:center; gap:10px; border:1.5px solid; border-radius:10px; padding:11px 14px; font-size:12.5px; font-weight:600; margin-bottom:18px;">
            <i class="fas fa-circle-exclamation" style="font-size:15px; flex-shrink:0;"></i>
            <span></span>
        </div>

        <!-- Mobile Banking -->
        <div class="pay-section-title">Mobile Banking</div>
        <div class="methods-grid">

            <!-- bKash — LIVE -->
            <button class="method-btn" onclick="executeBkashPayment(event)" id="bkash-btn">
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/1/10/Bkash_logo.svg/320px-Bkash_logo.svg.png"
                     alt="bKash" class="pay-logo" onerror="this.src='https://freepnglogos.com/uploads/bkash-logo-png/bkash-logo-png-1.png'">
                <div class="method-name">bKash</div>
                <div class="live-badge">LIVE</div>
            </button>

            <!-- Nagad -->
            <button class="method-btn disabled" onclick="showComingSoon('Nagad')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/4/4e/Nagad_Logo.svg/320px-Nagad_Logo.svg.png"
                     alt="Nagad" class="pay-logo" onerror="this.style.display='none'; this.nextSibling.style.display='block'">
                <div class="logo-fallback" style="display:none; background:#f15a29; color:white; font-weight:800; font-size:13px; width:72px; height:38px; border-radius:8px; display:none; align-items:center; justify-content:center;">nagad</div>
                <div class="method-name">Nagad</div>
            </button>

            <!-- Rocket -->
            <button class="method-btn disabled" onclick="showComingSoon('Rocket')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/3/32/Rocket_logo_%28Dutch-Bangla_Bank%29.svg/320px-Rocket_logo_%28Dutch-Bangla_Bank%29.svg.png"
                     alt="Rocket" class="pay-logo" onerror="this.src='https://i.ibb.co/k5B7dN7/rocket.png'">
                <div class="method-name">Rocket</div>
            </button>

            <!-- Upay -->
            <button class="method-btn disabled" onclick="showComingSoon('Upay')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/8/88/Upay_logo.svg/320px-Upay_logo.svg.png"
                     alt="Upay" class="pay-logo" onerror="this.style.filter='none'; this.alt='উপায়'">
                <div class="method-name">Upay (উপায়)</div>
            </button>

            <!-- MyCash -->
            <button class="method-btn disabled" onclick="showComingSoon('MyCash')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/7/7e/MyCash_logo.png/320px-MyCash_logo.png"
                     alt="MyCash" class="pay-logo">
                <div class="method-name">MyCash</div>
            </button>

            <!-- OKWallet (ওকে ওয়ালেট) -->
            <button class="method-btn disabled" onclick="showComingSoon('OK Wallet')">
                <div class="coming-soon">Soon</div>
                <div style="background:#fff200; border-radius:10px; width:72px; height:40px; display:flex; align-items:center; justify-content:center; font-weight:900; font-size:18px; color:#333; font-family:serif;">😊k</div>
                <div class="method-name">OK Wallet</div>
            </button>
        </div>

        <div class="divider"></div>

        <!-- Banks -->
        <div class="pay-section-title">Internet Banking</div>
        <div class="methods-grid" style="grid-template-columns: repeat(3, 1fr);">

            <!-- Dutch-Bangla Bank -->
            <button class="method-btn disabled" onclick="showComingSoon('Dutch-Bangla Bank')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/6/68/Dutch_Bangla_Bank_Logo.svg/320px-Dutch_Bangla_Bank_Logo.svg.png"
                     alt="DBBL" class="pay-logo">
                <div class="method-name">DBBL / Nexus</div>
            </button>

            <!-- City Bank -->
            <button class="method-btn disabled" onclick="showComingSoon('City Bank')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/4/4b/City_Bank_Bangladesh_Logo.svg/320px-City_Bank_Bangladesh_Logo.svg.png"
                     alt="City Bank" class="pay-logo" onerror="this.src='https://www.thecitybank.com/images/logo.png'">
                <div class="method-name">City Bank</div>
            </button>

            <!-- Islami Bank -->
            <button class="method-btn disabled" onclick="showComingSoon('Islami Bank')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/e/e9/Islami_Bank_Bangladesh_logo.svg/320px-Islami_Bank_Bangladesh_logo.svg.png"
                     alt="Islami Bank" class="pay-logo">
                <div class="method-name">Islami Bank</div>
            </button>

            <!-- Southeast Bank -->
            <button class="method-btn disabled" onclick="showComingSoon('Southeast Bank')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/a/a1/Southeast_Bank_Limited_logo.svg/320px-Southeast_Bank_Limited_logo.svg.png"
                     alt="Southeast Bank" class="pay-logo">
                <div class="method-name">Southeast Bank</div>
            </button>

            <!-- Meghna Bank -->
            <button class="method-btn disabled" onclick="showComingSoon('Meghna Bank')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/e/e5/Meghna_Bank_Logo.svg/320px-Meghna_Bank_Logo.svg.png"
                     alt="Meghna Bank" class="pay-logo">
                <div class="method-name">Meghna Bank</div>
            </button>

            <!-- MTB -->
            <button class="method-btn disabled" onclick="showComingSoon('Mutual Trust Bank')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/a/a0/MTB_Logo.svg/320px-MTB_Logo.svg.png"
                     alt="MTB" class="pay-logo" onerror="this.style.display='none'; this.parentNode.querySelector('.logo-fallback-mtb').style.display='flex'">
                <div class="logo-fallback-mtb" style="display:none; align-items:center; justify-content:center; font-weight:900; font-size:20px; color:#e30613;">MTB</div>
                <div class="method-name">MTB</div>
            </button>
        </div>

        <div class="divider"></div>

        <!-- Cards -->
        <div class="pay-section-title">Debit / Credit Cards</div>
        <div class="methods-grid" style="grid-template-columns: repeat(3, 1fr);">

            <!-- Visa -->
            <button class="method-btn disabled" onclick="showComingSoon('Visa')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/5/5e/Visa_Inc._logo.svg/320px-Visa_Inc._logo.svg.png"
                     alt="Visa" class="pay-logo" style="background:#1a1f71; border-radius:6px; padding:4px 8px;">
                <div class="method-name">Visa</div>
            </button>

            <!-- Mastercard -->
            <button class="method-btn disabled" onclick="showComingSoon('Mastercard')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/2/2a/Mastercard-logo.svg/320px-Mastercard-logo.svg.png"
                     alt="Mastercard" class="pay-logo">
                <div class="method-name">Mastercard</div>
            </button>

            <!-- American Express -->
            <button class="method-btn disabled" onclick="showComingSoon('American Express')">
                <div class="coming-soon">Soon</div>
                <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/f/fa/American_Express_logo_%282018%29.svg/320px-American_Express_logo_%282018%29.svg.png"
                     alt="Amex" class="pay-logo" style="background:#016fcf; border-radius:6px; padding:3px;">
                <div class="method-name">Amex</div>
            </button>
        </div>

        <div class="security-note">
            <i class="fas fa-shield-halved"></i>
            <div>All payments are <strong>SSL encrypted</strong> and processed through the official bKash Tokenized Payment Gateway. Your card or account details are never stored on our servers.</div>
        </div>

    </div>
</div>

<script>
    // ══════════════════════════════════════════
    // ১. URL থেকে UID বের করা + UID validation
    // ══════════════════════════════════════════
    const urlParams = new URLSearchParams(window.location.search);
    const userUid = urlParams.get('uid');

    const uidDisplay = document.getElementById('uid-display');

    if (userUid) {
        uidDisplay.textContent = userUid;
    } else {
        uidDisplay.textContent = "UID Not Found!";
        uidDisplay.style.color = "#ef4444";
        // UID না থাকলে সব "Choose & Pay" বাটন disable করে দাও
        document.querySelectorAll('.btn-primary, .btn-pink').forEach(btn => {
            btn.disabled = true;
            btn.style.opacity = "0.5";
            btn.style.cursor = "not-allowed";
            btn.title = "Please open this page from the RasFocus Desktop App.";
        });
        showPaymentNotice("Please open this page directly from your RasFocus Desktop Application.", "error");
    }

    // ── State ──
    let currentAmount = "500";
    let currentPlanName = "1 Year Ultimate";

    // ── Radio visual sync ──
    document.querySelectorAll('input[name="premium_plan"]').forEach(radio => {
        radio.addEventListener('change', () => {
            document.querySelectorAll('.sub-option').forEach(opt => {
                opt.style.borderColor = opt.querySelector('input').checked ? 'var(--teal)' : '';
                opt.style.background = opt.querySelector('input').checked ? 'var(--teal-light)' : '';
            });
        });
    });

    function selectFreeTrial() {
        alert("Your 14-Day trial started automatically on first app setup. After expiry, Free Basic activates automatically.");
    }

    function showPaymentMethods(type) {
        // UID না থাকলে payment section-এ যাওয়া বন্ধ
        if (!userUid) {
            showPaymentNotice("UID missing! Please open this page from the RasFocus Desktop App.", "error");
            return;
        }

        if (type === 'yearly') {
            currentAmount = "500";
            currentPlanName = "1 Year Ultimate";
        } else {
            const selected = document.querySelector('input[name="premium_plan"]:checked').value;
            if (selected === 'monthly') { currentAmount = "50"; currentPlanName = "1 Month Pro"; }
            else { currentAmount = "300"; currentPlanName = "6 Month Saver"; }
        }

        document.getElementById('summary-plan-name').textContent = currentPlanName;
        document.getElementById('summary-amount').textContent = "৳ " + currentAmount;

        // UID টি payment header-এ দেখাও
        document.getElementById('pay-uid').textContent = userUid;

        // Notice clear করে রাখো
        hidePaymentNotice();

        document.getElementById('pricing-section').classList.add('hidden');
        document.getElementById('payment-section').classList.remove('hidden');
        window.scrollTo({ top: 0, behavior: 'smooth' });
    }

    function goBackToPricing() {
        document.getElementById('payment-section').classList.add('hidden');
        document.getElementById('pricing-section').classList.remove('hidden');
        hidePaymentNotice();
        window.scrollTo({ top: 0, behavior: 'smooth' });
    }

    function showComingSoon(name) {
        alert(`${name} integration is coming soon! Currently in staging mode.`);
    }

    // ══════════════════════════════════════════
    // ২. bKash Payment — checkout এর exact logic
    // ══════════════════════════════════════════
    async function executeBkashPayment(e) {
        // Double-check UID (safety net)
        if (!userUid) {
            showPaymentNotice("Error: UID missing. Please open this page from your RasFocus Desktop Application.", "error");
            return;
        }

        const btn = e.currentTarget;
        const originalHTML = btn.innerHTML;
        const spinner = document.getElementById('spinner');

        // বাটনে loading state দেখাও
        btn.innerHTML = '<div style="width:18px;height:18px;border:2.5px solid rgba(255,255,255,0.4);border-top-color:white;border-radius:50%;animation:spin 0.7s linear infinite;"></div>';
        btn.disabled = true;
        hidePaymentNotice();
        spinner.classList.remove('hidden');

        try {
            const response = await fetch("https://rasfocus-backend.onrender.com/api/bkash/create", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    uid: userUid,
                    amount: currentAmount
                })
            });

            const data = await response.json();
            spinner.classList.add('hidden');

            // সার্ভার থেকে bKash URL পেলে সরাসরি redirect
            if (data.success && data.bkashURL) {
                window.location.href = data.bkashURL;
            } else {
                // Server error — notice দেখাও, বাটন restore করো
                showPaymentNotice("Payment initiation failed: " + (data.message || "Unknown error from gateway."), "error");
                resetBkashButton(btn, originalHTML);
            }

        } catch (err) {
            console.error("bKash Server Error:", err);
            spinner.classList.add('hidden');
            showPaymentNotice("Server connection failed. The backend may be waking up — please wait 30 seconds and try again.", "error");
            resetBkashButton(btn, originalHTML);
        }
    }

    // ── Helper: বাটন আগের অবস্থায় ফেরানো ──
    function resetBkashButton(btn, html) {
        btn.innerHTML = html;
        btn.disabled = false;
    }

    // ── Helper: payment section-এ notice দেখানো ──
    function showPaymentNotice(message, type) {
        const notice = document.getElementById('pay-notice');
        if (!notice) return;
        notice.querySelector('span').textContent = message;
        notice.style.display = 'flex';
        if (type === 'error') {
            notice.style.background = '#fef2f2';
            notice.style.borderColor = '#fecaca';
            notice.style.color = '#b91c1c';
        } else {
            notice.style.background = '#f0fdf9';
            notice.style.borderColor = '#99f6e4';
            notice.style.color = '#065f46';
        }
    }

    function hidePaymentNotice() {
        const notice = document.getElementById('pay-notice');
        if (notice) notice.style.display = 'none';
    }
</script>
</body>
</html>
