/*=====================================================================
  wifi_portal_html_styles.h - Portal CSS

  Extracted from wifi_portal_html.h. CSS-only, no template logic.
=====================================================================*/

#ifndef WIFI_PORTAL_HTML_STYLES_H
#define WIFI_PORTAL_HTML_STYLES_H

static const char PORTAL_HTML_STYLES[] PROGMEM = R"rawliteral(
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
  background:#0f1923;color:#c8d6e5;font-size:15px;max-width:480px;margin:0 auto}
.hdr{background:#1a2a3a;padding:16px;display:flex;justify-content:space-between;
  align-items:center;border-bottom:1px solid #2a3a4a}
.hdr h1{font-size:18px;color:#4cd97b;font-weight:600}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:6px}
.dot.on,.dot.ok{background:#4cd97b}.dot.off,.dot.err{background:#e74c3c}
.msg{display:none;padding:10px 16px;font-size:13px}
.msg.show{display:block}
.msg.info{background:#1d2f40;color:#a9c2d9}
.msg.ok{background:#1f3a2e;color:#9af0b5}
.msg.err{background:#4a2226;color:#ffb3bc}
.tabs{display:flex;background:#1a2a3a;border-bottom:1px solid #2a3a4a}
.tab{flex:1;padding:12px;text-align:center;cursor:pointer;color:#7f8c9b;
  border-bottom:2px solid transparent;transition:all .2s}
.tab.active{color:#4cd97b;border-bottom-color:#4cd97b}
.panel{display:none;padding:16px}.panel.active{display:block}
.card{background:#1a2a3a;border-radius:8px;padding:14px;margin-bottom:12px}
.card h3{font-size:13px;color:#7f8c9b;text-transform:uppercase;letter-spacing:1px;
  margin-bottom:10px}
.row{display:flex;justify-content:space-between;padding:6px 0;
  border-bottom:1px solid #0f1923}
.row:last-child{border:none}
.lbl{color:#7f8c9b}.val{color:#fff;font-weight:500}
.val.ok{color:#4cd97b}.val.warn{color:#f39c12}.val.err{color:#e74c3c}
select,input[type=number],input[type=text],input[type=password]{width:100%;padding:10px;background:#0f1923;color:#fff;
  border:1px solid #2a3a4a;border-radius:6px;font-size:15px;margin-top:4px}
select:focus,input:focus{outline:none;border-color:#4cd97b}
.checkline{display:flex;align-items:center;gap:8px;color:#c8d6e5;font-size:14px}
.checkline input{margin:0}
.btn{display:block;width:100%;padding:12px;border:none;border-radius:6px;
  font-size:15px;font-weight:600;cursor:pointer;margin-top:12px;transition:opacity .2s}
.btn:active{opacity:.7}
.btn-primary{background:#4cd97b;color:#0f1923}
.btn-action{background:#2a3a4a;color:#c8d6e5;margin-top:8px}
.btn-danger{background:#e74c3c;color:#fff;margin-top:8px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.info{font-size:12px;color:#556;text-align:center;padding:12px}
.field{margin-bottom:12px}
.field label{font-size:13px;color:#7f8c9b;display:block;margin-bottom:2px}
.hint{font-size:13px;line-height:1.45;color:#c8d6e5;background:#0f1923;border:1px solid #2a3a4a;
  border-radius:6px;padding:10px}
.phase-item{padding:6px 8px;border-radius:6px;border:1px solid transparent;margin-bottom:6px}
.phase-item:last-child{margin-bottom:0}
.phase-item.current{border-color:#4cd97b;background:rgba(76,217,123,0.08)}
.phase-item.override{border-color:#f39c12;background:rgba(243,156,18,0.12)}
.chip{display:inline-block;font-size:11px;padding:2px 6px;border-radius:999px;margin-left:6px}
.chip-current{background:#214a34;color:#9af0b5}
.chip-override{background:#5d3f13;color:#ffd08a}
.btn:disabled{opacity:.45;cursor:not-allowed}
.abar{margin:10px 16px;padding:12px 14px;border-radius:8px;border:1px solid #2a3a4a;background:#1a2a3a}
.abar-ok{border-color:#4cd97b;background:#12321c;color:#c8f5d5}
.abar-warn{border-color:#f39c12;background:#3a2e10;color:#ffd08a}
.abar-err{border-color:#ff5050;background:#3a1414;color:#ffb0b0}
.abar-title{font-weight:bold;font-size:15px;margin-bottom:4px}
.abar-msg{font-size:13px;line-height:1.5}
.tab.dim{color:#55606e;font-size:13px}
.tab.dim.active{color:#f39c12;border-bottom-color:#f39c12}
.chart-wrap{background:#0f1923;border:1px solid #2a3a4a;border-radius:6px;padding:8px;min-height:150px}
.chart-meta{font-size:12px;color:#7f8c9b;margin-top:8px}
.chart-empty{font-size:13px;color:#7f8c9b;text-align:center;padding:28px 8px}
.wifi-list{margin-top:10px;display:flex;flex-direction:column;gap:8px}
.wifi-item{background:#0f1923;border:1px solid #2a3a4a;border-radius:6px;padding:10px;display:flex;
  justify-content:space-between;align-items:center;cursor:pointer}
.wifi-item.active{border-color:#4cd97b;background:rgba(76,217,123,0.08)}
.wifi-name{font-size:14px;color:#fff;font-weight:600}
.wifi-meta{font-size:12px;color:#7f8c9b}
.auth-overlay{position:fixed;inset:0;background:rgba(5,9,13,0.82);display:none;
  align-items:center;justify-content:center;z-index:999;padding:16px}
.auth-overlay.show{display:flex}
.auth-card{background:#1a2a3a;border:1px solid #2a3a4a;border-radius:10px;padding:16px;width:100%;max-width:360px}
.auth-card h3{font-size:16px;color:#4cd97b;margin-bottom:6px}
.auth-card p{font-size:13px;color:#a9c2d9;line-height:1.45;margin-bottom:12px}
</style>
)rawliteral";

#endif // WIFI_PORTAL_HTML_STYLES_H
