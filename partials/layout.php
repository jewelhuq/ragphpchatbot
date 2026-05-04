<?php
// ============================================================
// layout.php — Shared header for all public pages
// ============================================================
// Variables expected from the including page:
//   $page  — current page slug ('home', 'pricing', etc.)
//   $title — browser tab title
// ============================================================
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= htmlspecialchars($title) ?></title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --dark:#0f1623;--dark2:#1a2236;--dark3:#16213e;
  --accent:#2563eb;--accent2:#1d4ed8;--accent-light:#eff6ff;
  --text:#1e293b;--muted:#64748b;--light:#f8fafc;--white:#fff;
  --border:#e2e8f0;--radius:12px;--shadow:0 4px 24px rgba(0,0,0,.08);
}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;color:var(--text);background:var(--white)}
a{color:inherit;text-decoration:none}

/* Header */
.site-header{
  background:var(--dark);padding:0 60px;height:68px;
  display:flex;align-items:center;justify-content:space-between;
  position:sticky;top:0;z-index:100;
  box-shadow:0 1px 0 rgba(255,255,255,.06);
}
.site-logo{color:#fff;font-size:20px;font-weight:800;letter-spacing:-.3px}
.site-logo span{color:var(--accent)}
.site-nav{display:flex;align-items:center;gap:6px}
.site-nav a{color:#94a3b8;font-size:14px;font-weight:500;padding:8px 14px;border-radius:8px;transition:.2s}
.site-nav a:hover{color:#fff;background:rgba(255,255,255,.06)}
.site-nav a.active{color:#fff}
.site-nav .nav-cta{background:var(--accent);color:#fff;padding:9px 20px;border-radius:8px;margin-left:8px;font-weight:600}
.site-nav .nav-cta:hover{background:var(--accent2)}

/* Shared layout helpers */
.container{max-width:1120px;margin:0 auto;padding:0 40px}
.section{padding:80px 0}
.section-gray{background:var(--light)}
.section-dark{background:var(--dark);color:#fff}
.section-label{font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.1em;color:var(--accent);margin-bottom:10px}
.section-title{font-size:36px;font-weight:800;line-height:1.2;margin-bottom:16px}
.section-sub{font-size:17px;color:var(--muted);line-height:1.7;max-width:560px}
.text-center{text-align:center}.text-center .section-sub{margin:0 auto}

.btn{display:inline-flex;align-items:center;gap:8px;padding:12px 24px;border-radius:var(--radius);font-size:15px;font-weight:600;cursor:pointer;border:none;transition:.2s}
.btn-accent{background:var(--accent);color:#fff}.btn-accent:hover{background:var(--accent2)}
.btn-outline{background:transparent;color:#fff;border:1px solid rgba(255,255,255,.2)}.btn-outline:hover{background:rgba(255,255,255,.08)}
.btn-dark{background:var(--dark);color:#fff}.btn-dark:hover{opacity:.9}
.btn-lg{padding:15px 32px;font-size:16px}

.card{background:#fff;border-radius:var(--radius);border:1px solid var(--border);padding:28px;box-shadow:var(--shadow)}
.card-icon{font-size:36px;margin-bottom:16px}
.card-title{font-size:17px;font-weight:700;margin-bottom:8px}
.card-desc{font-size:14px;color:var(--muted);line-height:1.7}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:24px}
.grid-3{display:grid;grid-template-columns:repeat(3,1fr);gap:24px}

/* Hero */
.hero{background:linear-gradient(135deg,var(--dark) 0%,var(--dark3) 60%,#0f3460 100%);color:#fff;padding:100px 0 80px}
.hero-title{font-size:52px;font-weight:800;line-height:1.15;margin-bottom:20px}
.hero-title span{color:var(--accent)}
.hero-sub{font-size:18px;color:#94a3b8;line-height:1.7;margin-bottom:36px;max-width:540px}
.hero-btns{display:flex;gap:12px;flex-wrap:wrap}

::-webkit-scrollbar{width:5px}::-webkit-scrollbar-track{background:transparent}::-webkit-scrollbar-thumb{background:#cbd5e1;border-radius:3px}
</style>
</head>
<body>
<header class="site-header">
  <a href="index.php" class="site-logo">Your<span>Brand</span></a>
  <nav class="site-nav">
    <a href="index.php" class="<?= $page==='home'?'active':'' ?>">Home</a>
    <a href="#" class="nav-cta">Get Started</a>
  </nav>
</header>
