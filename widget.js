// ============================================================
// RAG Chat Widget — with Session Persistence
// ============================================================
// Drop this script on any website with:
//
//   <script>
//     window.RAGConfig = {
//       widgetId: 'ws_xxxxxxxxxxxx',
//       apiUrl:   'https://yourserver.com/admin/api'
//     };
//   </script>
//   <script src="https://yourserver.com/widget/widget.js" defer></script>
//
// Features:
//   - Visitor capture form (name required, phone + email optional)
//   - Session stored in localStorage — survives page refresh
//   - All messages saved to Turso — viewable in admin panel
//   - FAQ tab: searchable accordion
//   - Chat tab: RAG streaming chatbot
// ============================================================

(function () {
  'use strict';

  const cfg       = window.RAGConfig || {};
  const WIDGET_ID = cfg.widgetId || '';
  const API_URL   = (cfg.apiUrl  || '').replace(/\/$/, '');
  const STORE_KEY = `rag_session_${WIDGET_ID}`;

  if (!WIDGET_ID || !API_URL) {
    console.warn('[RAGWidget] Missing RAGConfig.widgetId or RAGConfig.apiUrl');
    return;
  }

  // ----------------------------------------------------------
  // State
  // ----------------------------------------------------------
  let isOpen    = false;
  let activeTab = 'faq';
  let faqItems  = [];
  let history   = [];   // [{role, content}] — in-memory conversation
  let streaming = false;

  // Session persisted in localStorage
  let session = JSON.parse(localStorage.getItem(STORE_KEY) || 'null');
  // session = { session_id, name, phone, email } or null

  // ----------------------------------------------------------
  // Inject styles
  // ----------------------------------------------------------
  const style = document.createElement('style');
  style.textContent = `
    #rag-widget-btn {
      position:fixed;bottom:24px;right:24px;width:56px;height:56px;
      border-radius:50%;background:#2563eb;color:#fff;border:none;
      font-size:24px;cursor:pointer;box-shadow:0 4px 20px rgba(37,99,235,.4);
      z-index:99998;transition:.3s;display:flex;align-items:center;justify-content:center;
    }
    #rag-widget-btn:hover{transform:scale(1.08);background:#1d4ed8}
    #rag-widget-panel {
      position:fixed;bottom:92px;right:24px;width:380px;height:580px;
      background:#1a1a1a;border:1px solid #2a2a2a;border-radius:16px;
      box-shadow:0 20px 60px rgba(0,0,0,.6);z-index:99997;
      display:none;flex-direction:column;overflow:hidden;
      font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
    }
    #rag-widget-panel.open{display:flex}
    .rw-header{padding:16px 18px;background:#222;border-bottom:1px solid #2a2a2a;display:flex;align-items:center;gap:10px}
    .rw-title{font-size:15px;font-weight:600;color:#fff;flex:1}
    .rw-subtitle{font-size:11px;color:#555;margin-top:2px}
    .rw-close{background:none;border:none;color:#666;font-size:18px;cursor:pointer;padding:4px}
    .rw-close:hover{color:#fff}
    .rw-tabs{display:flex;border-bottom:1px solid #2a2a2a}
    .rw-tab{flex:1;padding:10px;text-align:center;font-size:13px;cursor:pointer;color:#666;
            border-bottom:2px solid transparent;transition:.2s;background:none;
            border-left:none;border-right:none;border-top:none}
    .rw-tab.active{color:#60a5fa;border-bottom-color:#2563eb}
    .rw-body{flex:1;overflow:hidden;display:flex;flex-direction:column}

    /* ---- Visitor capture form ---- */
    .rw-capture{flex:1;display:flex;flex-direction:column;justify-content:center;padding:24px;gap:14px}
    .rw-capture-title{font-size:16px;font-weight:600;color:#fff;text-align:center;margin-bottom:4px}
    .rw-capture-sub{font-size:13px;color:#64748b;text-align:center;margin-bottom:8px}
    .rw-input-group{display:flex;flex-direction:column;gap:4px}
    .rw-label{font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:.06em}
    .rw-input{background:#2a2a2a;border:1px solid #333;color:#e0e0e0;border-radius:8px;
              padding:10px 12px;font-size:13px;outline:none;width:100%;font-family:inherit}
    .rw-input:focus{border-color:#2563eb}
    .rw-input.error{border-color:#dc2626}
    .rw-btn{background:#2563eb;color:#fff;border:none;border-radius:8px;padding:12px;
            font-size:14px;font-weight:600;cursor:pointer;width:100%;transition:.2s;font-family:inherit}
    .rw-btn:hover{background:#1d4ed8}
    .rw-btn:disabled{opacity:.5;cursor:not-allowed}
    .rw-error-msg{color:#f87171;font-size:12px;text-align:center}

    /* ---- FAQ ---- */
    .rw-faq-search{padding:12px;border-bottom:1px solid #2a2a2a}
    .rw-faq-search input{width:100%;background:#2a2a2a;border:1px solid #333;color:#e0e0e0;
                         border-radius:8px;padding:8px 12px;font-size:13px;outline:none}
    .rw-faq-search input:focus{border-color:#2563eb}
    .rw-faq-list{flex:1;overflow-y:auto;padding:8px}
    .rw-faq-item{border:1px solid #2a2a2a;border-radius:10px;margin-bottom:8px;overflow:hidden}
    .rw-faq-q{padding:13px 16px;font-size:13px;font-weight:500;color:#e0e0e0;cursor:pointer;
              display:flex;justify-content:space-between;align-items:center}
    .rw-faq-q:hover{background:#222}
    .rw-faq-chevron{font-size:12px;color:#666;transition:.2s}
    .rw-faq-a{max-height:0;overflow:hidden;transition:.3s;background:#111;
              font-size:13px;color:#aaa;line-height:1.6;padding:0 16px}
    .rw-faq-a.open{max-height:400px;padding:12px 16px}
    .rw-no-faq{text-align:center;padding:40px 20px;color:#555;font-size:13px}
    .rw-vote-row{display:flex;align-items:center;gap:8px;margin-top:12px;padding-top:10px;border-top:1px solid #2a2a2a}
    .rw-vote-label{font-size:11px;color:#555;flex:1}
    .rw-vote-btn{background:none;border:1px solid #333;border-radius:6px;padding:4px 10px;cursor:pointer;font-size:14px;transition:.2s}
    .rw-vote-btn:hover{background:#2a2a2a;border-color:#555}
    .rw-vote-btn:disabled{cursor:default}
    .rw-vote-thanks{font-size:11px;color:#4ade80}

    /* ---- Chat ---- */
    .rw-chat-messages{flex:1;overflow-y:auto;padding:12px;display:flex;flex-direction:column;gap:12px}
    .rw-msg{max-width:88%;font-size:13px;line-height:1.5}
    .rw-msg.user{align-self:flex-end;background:#2563eb;color:#fff;padding:10px 14px;
                 border-radius:14px 14px 4px 14px}
    .rw-msg.assistant{align-self:flex-start;background:#2a2a2a;color:#e0e0e0;padding:10px 14px;
                      border-radius:14px 14px 14px 4px}
    .rw-msg.assistant.streaming::after{content:'▋';animation:rwBlink .7s infinite;color:#60a5fa}
    .rw-sources{font-size:11px;color:#555;margin-top:4px}
    .rw-chat-input{padding:12px;border-top:1px solid #2a2a2a;display:flex;gap:8px}
    .rw-chat-input textarea{flex:1;background:#2a2a2a;border:1px solid #333;color:#e0e0e0;
                            border-radius:10px;padding:9px 12px;font-size:13px;resize:none;
                            outline:none;font-family:inherit;max-height:100px;min-height:38px}
    .rw-chat-input textarea:focus{border-color:#2563eb}
    .rw-send{background:#2563eb;color:#fff;border:none;border-radius:10px;width:38px;height:38px;
             cursor:pointer;font-size:16px;display:flex;align-items:center;justify-content:center;flex-shrink:0}
    .rw-send:hover{background:#1d4ed8}
    .rw-send:disabled{opacity:.4;cursor:not-allowed}
    .rw-empty{text-align:center;padding:40px 20px;color:#555;font-size:13px}
    .rw-visitor-tag{background:#1e3a5f;color:#60a5fa;font-size:11px;padding:4px 10px;
                    border-radius:20px;display:inline-flex;align-items:center;gap:5px}
    .rw-powered{text-align:center;font-size:10px;color:#333;padding:6px;border-top:1px solid #1a1a1a}
    @keyframes rwBlink{0%,100%{opacity:1}50%{opacity:0}}
    .rw-live-badge{background:#16a34a;color:#fff;font-size:10px;font-weight:700;
                   padding:2px 8px;border-radius:20px;animation:rwPulse 2s infinite}
    @keyframes rwPulse{0%,100%{opacity:1}50%{opacity:.6}}
    .rw-msg.admin{align-self:flex-start;background:#16a34a;color:#fff;padding:10px 14px;
                  border-radius:14px 14px 14px 4px;font-size:13px;line-height:1.5}
    .rw-typing{align-self:flex-start;color:#555;font-size:12px;padding:4px 0}
    ::-webkit-scrollbar{width:4px}::-webkit-scrollbar-track{background:transparent}
    ::-webkit-scrollbar-thumb{background:#2a2a2a;border-radius:2px}
  `;
  document.head.appendChild(style);

  // ----------------------------------------------------------
  // Build DOM
  // ----------------------------------------------------------
  const btn = document.createElement('button');
  btn.id = 'rag-widget-btn';
  btn.innerHTML = '💬';
  btn.setAttribute('aria-label', 'Open chat');
  btn.onclick = toggleWidget;

  const panel = document.createElement('div');
  panel.id = 'rag-widget-panel';
  panel.innerHTML = `
    <div class="rw-header">
      <div>
        <div class="rw-title">Help Center</div>
        <div class="rw-subtitle" id="rw-visitor-tag"></div>
      </div>
      <button class="rw-close" onclick="document.getElementById('rag-widget-panel').classList.remove('open')" aria-label="Close">✕</button>
    </div>

    <!-- Visitor capture form — shown before first chat -->
    <div id="rw-capture-screen" class="rw-body" style="display:none">
      <div class="rw-capture">
        <div class="rw-capture-title">👋 Before we start</div>
        <div class="rw-capture-sub">Tell us a bit about yourself so we can help you better.</div>
        <div class="rw-input-group">
          <label class="rw-label">Your Name <span style="color:#dc2626">*</span></label>
          <input class="rw-input" id="rw-name" type="text" placeholder="John Smith" autocomplete="name">
        </div>
        <div class="rw-input-group">
          <label class="rw-label">Phone <span style="color:#555">(optional)</span></label>
          <input class="rw-input" id="rw-phone" type="tel" placeholder="+1 555 000 0000" autocomplete="tel">
        </div>
        <div class="rw-input-group">
          <label class="rw-label">Email <span style="color:#555">(optional)</span></label>
          <input class="rw-input" id="rw-email" type="email" placeholder="you@example.com" autocomplete="email">
        </div>
        <div class="rw-error-msg" id="rw-capture-error"></div>
        <button class="rw-btn" id="rw-start-btn" onclick="startSession()">Start Chat →</button>
      </div>
    </div>

    <!-- Main panel — shown after session starts -->
    <div id="rw-main-screen" style="display:none;flex:1;flex-direction:column;overflow:hidden">
      <div class="rw-tabs">
        <button class="rw-tab active" id="rw-tab-faq" onclick="rwSwitchTab('faq')">❓ FAQ</button>
        <button class="rw-tab" id="rw-tab-chat" onclick="rwSwitchTab('chat')">💬 Chat</button>
      </div>

      <!-- FAQ Tab -->
      <div id="rw-body-faq" class="rw-body">
        <div class="rw-faq-search">
          <input type="text" id="rw-faq-input" placeholder="Search FAQ..." oninput="rwSearchFaq(this.value)">
        </div>
        <div class="rw-faq-list" id="rw-faq-list">
          <div class="rw-no-faq">Loading...</div>
        </div>
      </div>

      <!-- Chat Tab -->
      <div id="rw-body-chat" class="rw-body" style="display:none">
        <div class="rw-chat-messages" id="rw-chat-messages">
          <div class="rw-empty" id="rw-chat-empty">Hi! Ask me anything about our products or services.</div>
        </div>
        <div class="rw-chat-input">
          <textarea id="rw-chat-input" placeholder="Type your question..." rows="1"
            onkeydown="if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();rwSend()}"
            oninput="this.style.height='auto';this.style.height=Math.min(this.scrollHeight,100)+'px'"></textarea>
          <button class="rw-send" id="rw-send-btn" onclick="rwSend()" aria-label="Send">➤</button>
        </div>
      </div>
    </div>

    <div class="rw-powered">Powered by RAG Assistant</div>
  `;

  document.body.appendChild(btn);
  document.body.appendChild(panel);

  // ----------------------------------------------------------
  // Toggle widget open/close
  // ----------------------------------------------------------
  function toggleWidget() {
    isOpen = !isOpen;
    panel.classList.toggle('open', isOpen);
    btn.innerHTML = isOpen ? '✕' : '💬';

    if (isOpen) {
      if (session) {
        // Already have a session — show main screen
        showMainScreen();
        if (faqItems.length === 0) loadFaq();
      } else {
        // New visitor — show capture form
        showCaptureScreen();
      }
    }
  }

  // ----------------------------------------------------------
  // Show/hide screens
  // ----------------------------------------------------------
  function showCaptureScreen() {
    document.getElementById('rw-capture-screen').style.display = 'flex';
    document.getElementById('rw-main-screen').style.display   = 'none';
    setTimeout(() => document.getElementById('rw-name').focus(), 100);
  }

  function showMainScreen() {
    document.getElementById('rw-capture-screen').style.display = 'none';
    document.getElementById('rw-main-screen').style.display    = 'flex';

    // Show visitor name in header
    if (session) {
      document.getElementById('rw-visitor-tag').innerHTML =
        `<span class="rw-visitor-tag">👤 ${rwEsc(session.name)}</span>`;
    }

    // Start polling for admin messages / mode changes
    startPolling();
  }

  // ----------------------------------------------------------
  // Start a new session — collect visitor info and call API
  // ----------------------------------------------------------
  window.startSession = async function () {
    const name  = document.getElementById('rw-name').value.trim();
    const phone = document.getElementById('rw-phone').value.trim();
    const email = document.getElementById('rw-email').value.trim();
    const errEl = document.getElementById('rw-capture-error');
    const btn   = document.getElementById('rw-start-btn');

    errEl.textContent = '';

    if (!name) {
      document.getElementById('rw-name').classList.add('error');
      errEl.textContent = 'Please enter your name to continue.';
      return;
    }

    btn.disabled     = true;
    btn.textContent  = 'Starting...';

    try {
      const res = await fetch(`${API_URL}/sessions.php?action=start`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ widget_id: WIDGET_ID, name, phone, email }),
      });
      const json = await res.json();
      if (!json.ok) throw new Error(json.error);

      // Save session to localStorage — survives page refresh
      session = { session_id: json.data.session_id, name, phone, email };
      localStorage.setItem(STORE_KEY, JSON.stringify(session));

      showMainScreen();
      loadFaq();

    } catch (e) {
      errEl.textContent = e.message || 'Failed to start session. Please try again.';
      btn.disabled     = false;
      btn.textContent  = 'Start Chat →';
    }
  };

  // Allow pressing Enter on the name field to submit
  document.getElementById('rw-name').addEventListener('keydown', e => {
    if (e.key === 'Enter') window.startSession();
  });

  // ----------------------------------------------------------
  // Tab switching
  // ----------------------------------------------------------
  window.rwSwitchTab = function (tab) {
    activeTab = tab;
    document.getElementById('rw-tab-faq').classList.toggle('active',  tab === 'faq');
    document.getElementById('rw-tab-chat').classList.toggle('active', tab === 'chat');
    document.getElementById('rw-body-faq').style.display  = tab === 'faq'  ? 'flex' : 'none';
    document.getElementById('rw-body-chat').style.display = tab === 'chat' ? 'flex' : 'none';
  };

  // ----------------------------------------------------------
  // FAQ — load from API and render as accordion
  // ----------------------------------------------------------
  async function loadFaq() {
    try {
      const res  = await fetch(`${API_URL}/widget_faq.php?widget_id=${WIDGET_ID}`);
      const json = await res.json();
      faqItems   = json.data || [];
      renderFaq(faqItems);
    } catch {
      document.getElementById('rw-faq-list').innerHTML =
        '<div class="rw-no-faq">Could not load FAQ. Please try again.</div>';
    }
  }

  function renderFaq(items) {
    const list = document.getElementById('rw-faq-list');
    if (!items.length) {
      list.innerHTML = '<div class="rw-no-faq">No FAQ items found.</div>';
      return;
    }
    list.innerHTML = items.map((f, i) => `
      <div class="rw-faq-item" data-faq-id="${f.id}">
        <div class="rw-faq-q" onclick="rwToggleFaq(${i}, '${f.id}')">
          <span>${rwEsc(f.question)}</span>
          <span class="rw-faq-chevron" id="rw-chev-${i}">▼</span>
        </div>
        <div class="rw-faq-a" id="rw-ans-${i}">
          ${rwMarkdown(f.answer)}
          <div class="rw-vote-row" id="rw-vote-${f.id}">
            <span class="rw-vote-label">Was this helpful?</span>
            <button class="rw-vote-btn" id="rw-up-${f.id}" onclick="rwVote('${f.id}','up')">👍</button>
            <button class="rw-vote-btn" id="rw-dn-${f.id}" onclick="rwVote('${f.id}','down')">👎</button>
            <span class="rw-vote-thanks" id="rw-vt-${f.id}" style="display:none">Thanks for your feedback!</span>
          </div>
        </div>
      </div>
    `).join('');
  }

  // track which FAQs we've already recorded a view for this session
  const viewedFaqs = new Set();

  window.rwToggleFaq = function (i, faqId) {
    const ans  = document.getElementById(`rw-ans-${i}`);
    const chev = document.getElementById(`rw-chev-${i}`);
    const open = ans.classList.toggle('open');
    chev.style.transform = open ? 'rotate(180deg)' : '';

    // Track view once per FAQ per widget session
    if (open && faqId && !viewedFaqs.has(faqId)) {
      viewedFaqs.add(faqId);
      fetch(`${API_URL}/faq_analytics.php?action=view`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({
          faq_id:     faqId,
          widget_id:  WIDGET_ID,
          session_id: session?.session_id || '',
        }),
      }).catch(() => {}); // fire-and-forget, never block the UI
    }
  };

  // Vote on a FAQ item — 👍 or 👎
  window.rwVote = async function (faqId, vote) {
    const upBtn  = document.getElementById(`rw-up-${faqId}`);
    const dnBtn  = document.getElementById(`rw-dn-${faqId}`);
    const thanks = document.getElementById(`rw-vt-${faqId}`);

    // Highlight chosen vote
    upBtn.style.opacity = vote === 'up'   ? '1'   : '0.4';
    dnBtn.style.opacity = vote === 'down' ? '1'   : '0.4';
    upBtn.disabled      = true;
    dnBtn.disabled      = true;
    thanks.style.display = 'inline';

    try {
      await fetch(`${API_URL}/faq_analytics.php?action=vote`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({
          faq_id:     faqId,
          widget_id:  WIDGET_ID,
          vote:       vote,
          session_id: session?.session_id || '',
        }),
      });
    } catch { /* silent — vote already shown optimistically */ }
  };

  let faqTimer;
  window.rwSearchFaq = async function (q) {
    clearTimeout(faqTimer);
    faqTimer = setTimeout(async () => {
      if (!q.trim()) { renderFaq(faqItems); return; }
      try {
        const res  = await fetch(`${API_URL}/widget_faq.php?widget_id=${WIDGET_ID}&q=${encodeURIComponent(q)}`);
        const json = await res.json();
        renderFaq(json.data || []);
      } catch { renderFaq([]); }
    }, 300);
  };

  // ----------------------------------------------------------
  // Chat — SSE streaming with session persistence
  // ----------------------------------------------------------
  window.rwSend = function () {
    const input = document.getElementById('rw-chat-input');
    const text  = input.value.trim();
    if (!text || streaming) return;

    // If admin has taken over, don't send to AI — just save user message and wait
    if (currentMode === 'human') {
      appendMessage('user', text);
      history.push({ role: 'user', content: text });
      input.value = '';
      input.style.height = 'auto';
      // Save user message to session
      fetch(`${API_URL}/sessions.php?action=save_message`, {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({session_id: session.session_id, role: 'user', content: text})
      }).catch(()=>{});
      return;
    }

    // Remove empty state message on first chat
    const empty = document.getElementById('rw-chat-empty');
    if (empty) empty.remove();

    appendMessage('user', text);
    history.push({ role: 'user', content: text });

    input.value         = '';
    input.style.height  = 'auto';
    streaming           = true;
    document.getElementById('rw-send-btn').disabled = true;

    const bubble = appendMessage('assistant', '');
    bubble.classList.add('streaming');
    let fullResponse = '';

    // Build SSE URL — include session_id so server saves the conversation
    const params = new URLSearchParams({
      widget_id:  WIDGET_ID,
      q:          text,
      session_id: session?.session_id || '',
      history:    JSON.stringify(history.slice(0, -1)),
    });

    const evtSrc = new EventSource(`${API_URL}/widget_chat.php?${params}`);

    evtSrc.addEventListener('token', e => {
      fullResponse += e.data.replace(/\\n/g, '\n');
      bubble.textContent = fullResponse;
      bubble.classList.add('streaming');
      scrollChat();
    });

    evtSrc.addEventListener('sources', e => {
      const sources = JSON.parse(e.data);
      if (sources.length) {
        const src      = document.createElement('div');
        src.className  = 'rw-sources';
        src.textContent = '📄 ' + sources.join(', ');
        bubble.parentNode.insertBefore(src, bubble.nextSibling);
      }
    });

    evtSrc.addEventListener('done', () => {
      evtSrc.close();
      bubble.classList.remove('streaming');
      history.push({ role: 'assistant', content: fullResponse });
      if (history.length > 20) history = history.slice(-20);
      streaming = false;
      document.getElementById('rw-send-btn').disabled = false;
    });

    evtSrc.onerror = () => {
      evtSrc.close();
      bubble.classList.remove('streaming');
      if (!fullResponse) bubble.textContent = 'Sorry, something went wrong. Please try again.';
      streaming = false;
      document.getElementById('rw-send-btn').disabled = false;
    };
  };

  // ----------------------------------------------------------
  // Poll for admin messages when in human mode
  // ----------------------------------------------------------
  let pollInterval  = null;
  let lastMessageId = 0;
  let currentMode   = 'ai'; // 'ai' or 'human'

  function startPolling() {
    if (pollInterval) return;
    pollInterval = setInterval(pollForMessages, 2000);
  }

  function stopPolling() {
    if (pollInterval) { clearInterval(pollInterval); pollInterval = null; }
  }

  async function pollForMessages() {
    if (!session?.session_id) return;
    try {
      const r = await fetch(`${API_URL}/live_chat.php?action=poll&session_id=${session.session_id}&since=${lastMessageId}`);
      const j = await r.json();
      if (!j.ok) return;

      const newMode = j.data.mode;
      const msgs    = j.data.messages || [];

      // Mode changed to human — show live badge
      if (newMode === 'human' && currentMode === 'ai') {
        currentMode = 'human';
        showLiveBadge(true);
      }
      // Mode changed back to ai
      if (newMode === 'ai' && currentMode === 'human') {
        currentMode = 'ai';
        showLiveBadge(false);
      }

      // Append new messages from admin
      msgs.forEach(msg => {
        if (msg.id > lastMessageId) lastMessageId = parseInt(msg.id);
        if (msg.role === 'admin') {
          const empty = document.getElementById('rw-chat-empty');
          if (empty) empty.remove();
          const el = document.createElement('div');
          el.className = 'rw-msg admin';
          el.textContent = msg.content;
          document.getElementById('rw-chat-messages').appendChild(el);
          scrollChat();
        }
      });
    } catch {}
  }

  function showLiveBadge(live) {
    const subtitle = document.getElementById('rw-visitor-tag');
    if (live) {
      // Add live badge next to visitor name
      const existing = document.getElementById('rw-live-badge');
      if (!existing) {
        const badge = document.createElement('span');
        badge.id = 'rw-live-badge';
        badge.className = 'rw-live-badge';
        badge.textContent = '🟢 Live Support';
        subtitle.appendChild(document.createTextNode(' '));
        subtitle.appendChild(badge);
      }
    } else {
      const b = document.getElementById('rw-live-badge');
      if (b) b.remove();
    }
  }

  function appendMessage(role, text) {
    const messages = document.getElementById('rw-chat-messages');
    const el       = document.createElement('div');
    el.className   = `rw-msg ${role}`;
    el.textContent = text;
    messages.appendChild(el);
    scrollChat();
    return el;
  }

  function scrollChat() {
    const el  = document.getElementById('rw-chat-messages');
    el.scrollTop = el.scrollHeight;
  }

  // ----------------------------------------------------------
  // If returning visitor — restore their session greeting
  // but don't reload chat history (keep it lightweight)
  // ----------------------------------------------------------
  if (session) {
    // Session exists from a previous visit — update header when panel opens
  }

  // ----------------------------------------------------------
  // Helpers
  // ----------------------------------------------------------
  function rwEsc(str) {
    return String(str || '')
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function rwMarkdown(text) {
    return rwEsc(text)
      .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
      .replace(/\*(.+?)\*/g,     '<em>$1</em>')
      .replace(/\[(.+?)\]\((.+?)\)/g, '<a href="$2" target="_blank" style="color:#60a5fa">$1</a>')
      .replace(/\n/g, '<br>');
  }

})();
