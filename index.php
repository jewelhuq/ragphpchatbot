<?php
// ============================================================
// index.php — Public homepage template
// ============================================================
// Replace all placeholder text with your own content.
// The RAG chat widget appears in the bottom-right corner.
// ============================================================
$page  = 'home';
$title = 'Your Brand — Tagline Here';
include 'partials/layout.php';
?>

<!-- ============================================================
     HERO — Replace with your headline and description
     ============================================================ -->
<section class="hero">
  <div class="container">
    <div class="hero-title">Your Headline<br><span>Goes Here</span></div>
    <p class="hero-sub">Replace this with your product description. Tell visitors what you do and why they should care.</p>
    <div class="hero-btns">
      <a href="#" class="btn btn-accent btn-lg">Get Started Free</a>
      <a href="#" class="btn btn-outline btn-lg">Learn More</a>
    </div>
  </div>
</section>

<!-- ============================================================
     FEATURES — Replace with your product features
     ============================================================ -->
<section class="section">
  <div class="container">
    <div class="text-center" style="margin-bottom:52px">
      <div class="section-label">Features</div>
      <h2 class="section-title">Everything You Need</h2>
      <p class="section-sub">Replace this with a short description of your feature set.</p>
    </div>
    <div class="grid-3">
      <div class="card">
        <div class="card-icon">⚡</div>
        <h3 class="card-title">Feature One</h3>
        <p class="card-desc">Describe your first key feature here. Keep it concise and benefit-focused.</p>
      </div>
      <div class="card">
        <div class="card-icon">🛡️</div>
        <h3 class="card-title">Feature Two</h3>
        <p class="card-desc">Describe your second key feature here. Focus on the value it delivers.</p>
      </div>
      <div class="card">
        <div class="card-icon">🔗</div>
        <h3 class="card-title">Feature Three</h3>
        <p class="card-desc">Describe your third key feature here. What problem does it solve?</p>
      </div>
      <div class="card">
        <div class="card-icon">📊</div>
        <h3 class="card-title">Feature Four</h3>
        <p class="card-desc">Add more features as needed. Duplicate this card block.</p>
      </div>
      <div class="card">
        <div class="card-icon">🌐</div>
        <h3 class="card-title">Feature Five</h3>
        <p class="card-desc">Keep descriptions short — one or two sentences is ideal.</p>
      </div>
      <div class="card">
        <div class="card-icon">💳</div>
        <h3 class="card-title">Feature Six</h3>
        <p class="card-desc">Use emoji or icons to make features visually scannable.</p>
      </div>
    </div>
  </div>
</section>

<!-- ============================================================
     CTA — Replace with your call to action
     ============================================================ -->
<section class="section section-dark">
  <div class="container text-center">
    <h2 class="section-title" style="color:#fff">Ready to Get Started?</h2>
    <p class="section-sub" style="margin:0 auto 32px;color:#94a3b8">Replace this with your CTA description.</p>
    <a href="#" class="btn btn-accent btn-lg">Start Free — No Credit Card</a>
  </div>
</section>

<?php include 'partials/footer.php'; ?>
