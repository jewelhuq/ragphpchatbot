<?php // Shared footer + widget script ?>
<footer style="background:var(--dark);color:#475569;padding:32px 60px;text-align:center;font-size:13px">
  <div style="margin-bottom:8px">
    <a href="index.php" style="color:#94a3b8;margin:0 12px">Home</a>
  </div>
  <div>© <?= date('Y') ?> YourBrand. All rights reserved.</div>
</footer>

<!-- ============================================================
     RAG Chat Widget
     Replace widget_id with your workspace widget ID from the admin panel.
     Replace apiUrl with your server's admin API URL.
     ============================================================ -->
<script>
  window.RAGConfig = {
    widgetId: 'ws_32e2ee7dfd06f499471208f6',
    apiUrl:   'http://localhost:8081/api'
  };
</script>
<script src="/widget.js" defer></script>
</body>
</html>
