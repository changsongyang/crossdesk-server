#include "admin_controller.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <string>

namespace {

constexpr char kSessionCookieName[] = "cd_admin_session";
constexpr char kDisconnectPrefix[] = "/api/admin/sessions/";
constexpr char kDisconnectSuffix[] = "/disconnect";
constexpr size_t kDefaultPageLimit = 50;
constexpr size_t kMaxPageLimit = 200;

std::string ResourcePath(const std::string& resource) {
  size_t query_pos = resource.find('?');
  return query_pos == std::string::npos ? resource
                                        : resource.substr(0, query_pos);
}

std::string ResourceQuery(const std::string& resource) {
  size_t query_pos = resource.find('?');
  return query_pos == std::string::npos ? "" : resource.substr(query_pos + 1);
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

std::string UrlDecode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    char ch = value[i];
    if (ch == '+') {
      decoded.push_back(' ');
    } else if (ch == '%' && i + 2 < value.size()) {
      int hi = HexValue(value[i + 1]);
      int lo = HexValue(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        decoded.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        decoded.push_back(ch);
      }
    } else {
      decoded.push_back(ch);
    }
  }
  return decoded;
}

std::map<std::string, std::string> ParseQueryParams(
    const std::string& query_string) {
  std::map<std::string, std::string> params;
  size_t start = 0;
  while (start <= query_string.size()) {
    size_t end = query_string.find('&', start);
    std::string item =
        query_string.substr(start, end == std::string::npos
                                       ? std::string::npos
                                       : end - start);
    if (!item.empty()) {
      size_t equals = item.find('=');
      std::string key = UrlDecode(item.substr(0, equals));
      std::string value = equals == std::string::npos
                              ? ""
                              : UrlDecode(item.substr(equals + 1));
      params[key] = value;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return params;
}

bool ParseSize(const std::string& value, size_t* result) {
  if (value.empty()) {
    return false;
  }
  size_t parsed = 0;
  for (unsigned char ch : value) {
    if (!std::isdigit(ch)) {
      return false;
    }
    size_t digit = static_cast<size_t>(ch - '0');
    if (parsed > (static_cast<size_t>(-1) - digit) / 10) {
      return false;
    }
    parsed = parsed * 10 + digit;
  }
  *result = parsed;
  return true;
}

size_t QuerySizeParam(const std::map<std::string, std::string>& params,
                      const std::string& key, size_t fallback,
                      size_t max_value) {
  auto it = params.find(key);
  if (it == params.end()) {
    return fallback;
  }
  size_t value = 0;
  if (!ParseSize(it->second, &value)) {
    return fallback;
  }
  return std::min(value, max_value);
}

std::string QueryStringParam(const std::map<std::string, std::string>& params,
                             const std::string& key) {
  auto it = params.find(key);
  return it == params.end() ? "" : it->second;
}

std::string QueryStringParam(const std::map<std::string, std::string>& params,
                             const std::string& key,
                             const std::string& fallback) {
  auto it = params.find(key);
  return it == params.end() || it->second.empty() ? fallback : it->second;
}

std::string ClientKind(const std::string& device_id) {
  return device_id.rfind("web-", 0) == 0 ? "web" : "device";
}

const char kAdminHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CrossDesk Admin</title>
  <style>
    :root { color-scheme: light; font-family: "Segoe UI", Arial, sans-serif; }
    * { box-sizing: border-box; }
    body { margin: 0; background: #f4f6f8; color: #17202a; -webkit-text-size-adjust: 100%; }
    header { background: #ffffff; border-bottom: 1px solid #d9dee5; padding: 14px 24px; display: flex; align-items: center; justify-content: space-between; }
    h1 { font-size: 20px; margin: 0; }
    main { padding: 24px; max-width: 1280px; margin: 0 auto; }
    button { border: 1px solid #9aa7b5; background: #ffffff; border-radius: 6px; cursor: pointer; font: inherit; min-height: 38px; padding: 8px 12px; }
    button.primary { background: #1264a3; color: #ffffff; border-color: #1264a3; }
    button.danger { background: #b42318; color: #ffffff; border-color: #b42318; }
    button:disabled { opacity: .6; cursor: not-allowed; }
    input { border: 1px solid #b8c2cc; border-radius: 6px; font: inherit; min-height: 38px; padding: 9px 10px; }
    .login { min-height: 70vh; display: grid; place-items: center; }
    .panel { background: #ffffff; border: 1px solid #d9dee5; border-radius: 8px; min-width: 0; padding: 18px; }
    .login .panel { width: min(360px, calc(100vw - 40px)); display: grid; gap: 12px; }
    .metrics { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 14px; margin-bottom: 18px; }
    .metric { background: #ffffff; border: 1px solid #d9dee5; border-radius: 8px; min-width: 0; padding: 16px; }
    .metric span { color: #667085; font-size: 13px; }
    .metric strong { display: block; font-size: 30px; margin-top: 6px; }
    .geo-panel { margin-bottom: 18px; }
    .geo-total { color: #667085; font-size: 13px; }
    .geo-total strong { color: #17202a; font-size: 18px; margin-left: 4px; }
    .geo-layout { display: grid; grid-template-columns: minmax(0, 1fr) 220px; gap: 18px; align-items: stretch; }
    .china-map-wrap { min-height: 360px; position: relative; }
    .china-map { display: block; height: 100%; min-height: 360px; width: 100%; }
    .china-map .province { cursor: pointer; stroke: #ffffff; stroke-linejoin: round; stroke-width: 2; transition: fill .16s ease, stroke .16s ease, transform .16s ease; }
    .china-map .province:hover { stroke: #1264a3; stroke-width: 3; }
    .china-map text { fill: #344054; font-size: 12px; font-weight: 600; pointer-events: none; text-anchor: middle; }
    .geo-tooltip { background: #17202a; border-radius: 6px; color: #ffffff; display: none; font-size: 13px; left: 0; max-width: 220px; padding: 8px 10px; pointer-events: none; position: absolute; top: 0; transform: translate(10px, 10px); z-index: 3; }
    .geo-tooltip.visible { display: block; }
    .geo-side { display: grid; align-content: start; gap: 14px; min-width: 0; }
    .geo-stat span, .geo-list-title { color: #667085; display: block; font-size: 12px; }
    .geo-stat strong { display: block; font-size: 28px; margin-top: 3px; }
    .geo-stat small { color: #667085; display: block; font-size: 12px; margin-top: 2px; }
    .geo-top-list { color: #344054; font-size: 13px; margin: 6px 0 0; padding-left: 20px; }
    .geo-top-list li { margin: 4px 0; }
    .grid { display: grid; grid-template-columns: minmax(0, 1.1fr) minmax(0, .9fr); gap: 18px; align-items: start; }
    .toolbar { display: flex; gap: 10px; justify-content: space-between; align-items: center; margin-bottom: 12px; min-width: 0; }
    .toolbar h2 { font-size: 18px; margin: 0; }
    .actions { display: flex; gap: 8px; align-items: center; justify-content: flex-end; flex-wrap: wrap; min-width: 0; }
    select { border: 1px solid #b8c2cc; border-radius: 6px; font: inherit; min-height: 38px; padding: 9px 10px; background: #ffffff; }
    .table-wrap { max-width: 100%; overflow-x: auto; }
    table { width: 100%; border-collapse: collapse; font-size: 14px; }
    th, td { border-bottom: 1px solid #edf0f3; padding: 10px 8px; text-align: left; vertical-align: top; }
    th { color: #667085; font-weight: 600; }
    .segments { display: flex; gap: 6px; margin: 0 0 12px; overflow-x: auto; padding-bottom: 2px; }
    .segments button { white-space: nowrap; border-color: #d0d5dd; color: #344054; }
    .segments button.active { background: #1264a3; border-color: #1264a3; color: #ffffff; }
    .presence-table { table-layout: fixed; }
    .presence-table th, .presence-table td { min-width: 0; overflow-wrap: anywhere; }
    .presence-table th:nth-child(1), .presence-table td:nth-child(1) { width: 23%; }
    .presence-table th:nth-child(2), .presence-table td:nth-child(2) { width: 10%; }
    .presence-table th:nth-child(3), .presence-table td:nth-child(3) { width: 18%; }
    .presence-table th:nth-child(4), .presence-table td:nth-child(4) { width: 17%; }
    .presence-table th:nth-child(5), .presence-table td:nth-child(5) { width: 13%; }
    .presence-table th:nth-child(6), .presence-table td:nth-child(6) { width: 9%; }
    .presence-table th:nth-child(7), .presence-table td:nth-child(7) { width: 10%; }
    .presence-table th:nth-child(6), .presence-table td:nth-child(6),
    .presence-table th:nth-child(7), .presence-table td:nth-child(7) { overflow-wrap: normal; }
    .presence-table td:nth-child(7) button { min-width: 0; padding: 6px 8px; white-space: nowrap; width: 100%; }
    .sessions-table td:nth-child(1), .sessions-table td:nth-child(2) { word-break: break-all; }
    .device-id { font-weight: 600; word-break: break-all; }
    .subline { display: block; color: #667085; font-size: 12px; margin-top: 3px; }
    .badge { border-radius: 999px; display: inline-flex; align-items: center; font-size: 12px; font-weight: 700; line-height: 1; padding: 5px 8px; }
    .badge.online { background: #dcfae6; color: #067647; }
    .badge.offline { background: #f2f4f7; color: #475467; }
    .badge.active { background: #fef0c7; color: #b54708; margin-left: 6px; }
    .badge.web { background: #e0f2fe; color: #026aa2; }
    .details-row td { background: #f9fafb; }
    .detail-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 10px 16px; }
    .detail-grid span { color: #667085; display: block; font-size: 12px; }
    .detail-grid strong { display: block; font-size: 14px; margin-top: 2px; }
    .peer-list { word-break: break-all; }
    .sort-order { min-width: 62px; }
    .pager { display: flex; gap: 8px; align-items: center; justify-content: flex-end; margin-top: 12px; color: #667085; font-size: 13px; flex-wrap: wrap; }
    .status { color: #067647; font-weight: 600; }
    .offline { color: #667085; font-weight: 600; }
    .muted { color: #667085; }
    .error { color: #b42318; min-height: 20px; }
    .empty { color: #667085; text-align: center; padding: 16px 8px; }
    .hidden { display: none; }
    @media (max-width: 860px) {
      header { padding: 12px 16px; }
      main { padding: 16px; }
      .metrics, .geo-layout, .grid { grid-template-columns: 1fr; }
      .toolbar { align-items: stretch; flex-direction: column; gap: 8px; }
      .actions { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); justify-content: stretch; width: 100%; }
      .actions input, .actions select, .actions button { min-width: 0; width: 100%; }
      .actions .error { grid-column: 1 / -1; }
      #dashboard-view > .toolbar .actions { grid-template-columns: 1fr; }
      .sort-order { min-width: 0; }
    }
    @media (max-width: 640px) {
      header { gap: 12px; }
      h1 { font-size: 18px; }
      button, input, select { min-height: 44px; }
      main { padding: 12px; }
      .login { align-items: start; min-height: calc(100vh - 58px); padding-top: 32px; }
      .login .panel { width: 100%; }
      .panel { padding: 14px; }
      .metrics { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px; margin-bottom: 12px; }
      .metric { padding: 12px; }
      .metric strong { font-size: 24px; margin-top: 4px; }
      .china-map-wrap, .china-map { min-height: 300px; }
      .grid { gap: 12px; }
      .segments { margin: 0 -2px 12px; padding-bottom: 6px; }
      .segments button { min-height: 40px; padding: 8px 10px; }
      .table-wrap { overflow: visible; }
      table, tbody, tr, td { display: block; width: 100%; }
      thead { display: none; }
      tbody tr { background: #ffffff; border: 1px solid #d9dee5; border-radius: 8px; margin-bottom: 10px; padding: 10px 12px; }
      tbody tr.device-row.expanded { border-bottom-left-radius: 0; border-bottom-right-radius: 0; margin-bottom: 0; }
      tbody tr.details-row { background: #f9fafb; border-top: 0; border-top-left-radius: 0; border-top-right-radius: 0; margin-top: 0; padding: 0 12px 12px; }
      tbody td { align-items: start; border-bottom: 0; display: grid; gap: 8px; grid-template-columns: minmax(92px, 34%) minmax(0, 1fr); padding: 8px 0; }
      tbody td::before { color: #667085; content: attr(data-label); font-size: 12px; font-weight: 600; }
      tbody td.empty { display: block; padding: 14px 0; text-align: center; }
      tbody td.empty::before, tbody tr.details-row td::before { content: none; display: none; }
      tbody tr.details-row td { background: transparent; display: block; padding: 0; }
      tbody td[data-label="Action"] { display: block; }
      tbody td[data-label="Action"]::before { content: none; display: none; }
      tbody td[data-label="Action"] button { width: 100%; }
      .detail-grid { grid-template-columns: 1fr; gap: 8px; }
      .pager { justify-content: space-between; }
      .pager span { flex: 1; text-align: center; }
      .pager button { min-width: 96px; }
    }
    @media (max-width: 420px) {
      .actions { grid-template-columns: 1fr; }
      .metrics { grid-template-columns: 1fr; }
      .pager { display: grid; grid-template-columns: 1fr 1fr; width: 100%; }
      .pager span { grid-column: 1 / -1; order: -1; text-align: left; }
      .pager button { min-width: 0; width: 100%; }
    }
  </style>
</head>
<body>
  <header>
    <h1>CrossDesk Admin</h1>
    <button id="logout" class="hidden">Logout</button>
  </header>
  <main>
    <section id="login-view" class="login">
      <form id="login-form" class="panel">
        <h2>Admin Login</h2>
        <input id="username" autocomplete="username" placeholder="Username" required>
        <input id="password" autocomplete="current-password" placeholder="Password" type="password" required>
        <button class="primary" type="submit">Login</button>
        <div id="login-error" class="error"></div>
      </form>
    </section>
    <section id="dashboard-view" class="hidden">
      <div class="toolbar">
        <div class="muted">Last refresh: <span id="last-refresh">never</span></div>
        <div class="actions">
          <button id="list-refresh" type="button">Refresh lists</button>
          <div id="refresh-error" class="error"></div>
        </div>
      </div>
      <div class="metrics">
        <div class="metric"><span>Online devices</span><strong id="metric-devices">0</strong></div>
        <div class="metric"><span>Web clients</span><strong id="metric-web">0</strong></div>
        <div class="metric"><span>Active sessions</span><strong id="metric-sessions">0</strong></div>
        <div class="metric"><span>Online time</span><strong id="metric-duration">0s</strong></div>
        <div class="metric"><span>Control time</span><strong id="metric-control">0s</strong></div>
        <div class="metric"><span>Controlled time</span><strong id="metric-controlled">0s</strong></div>
      </div>
      <section class="panel geo-panel">
        <div class="toolbar">
          <h2>中国用户分布</h2>
          <div class="geo-total">总用户 <strong id="geo-total">0</strong></div>
        </div>
        <div class="geo-layout">
          <div class="china-map-wrap">
            <svg id="china-map" class="china-map" viewBox="0 0 620 620" role="img" aria-label="China user distribution map"></svg>
            <div id="geo-tooltip" class="geo-tooltip"></div>
          </div>
          <aside class="geo-side">
            <div class="geo-stat">
              <span>国外用户</span>
              <strong id="geo-foreign-count">0</strong>
              <small id="geo-foreign-percent">0%</small>
            </div>
            <div class="geo-stat">
              <span>未解析</span>
              <strong id="geo-unknown-count">0</strong>
              <small id="geo-unknown-percent">0%</small>
            </div>
            <div class="geo-list">
              <span class="geo-list-title">省份排行</span>
              <ol id="geo-top-provinces" class="geo-top-list"></ol>
            </div>
          </aside>
        </div>
      </section>
      <div class="grid">
        <section class="panel">
          <div class="toolbar">
            <h2>Client Presence</h2>
            <div class="actions">
              <input id="device-search" placeholder="Search device ID">
              <select id="device-sort" aria-label="Device sort">
                <option value="status">Status</option>
                <option value="updated_at">Last seen</option>
                <option value="online_since">Online since</option>
                <option value="current_online">Current online</option>
                <option value="total_online">Total online</option>
                <option value="total_control">Total control</option>
                <option value="total_controlled">Total controlled</option>
                <option value="location">Location</option>
                <option value="device_id">Device ID</option>
              </select>
              <button id="device-order" class="sort-order" type="button" aria-label="Toggle sort order">DESC</button>
              <select id="device-limit" aria-label="Devices per page">
                <option value="50">50 / page</option>
                <option value="100">100 / page</option>
                <option value="200">200 / page</option>
              </select>
            </div>
          </div>
          <div class="segments" id="device-filters" role="tablist" aria-label="Device filters">
            <button type="button" data-device-filter="online">Online <span id="device-count-online">0</span></button>
            <button type="button" data-device-filter="active">Remote <span id="device-count-active">0</span></button>
            <button type="button" data-device-filter="offline">Offline <span id="device-count-offline">0</span></button>
            <button type="button" data-device-filter="all">All <span id="device-count-all">0</span></button>
            <button type="button" data-device-filter="web">Web <span id="device-count-web">0</span></button>
          </div>
          <div class="table-wrap">
            <table class="presence-table">
              <thead><tr><th>Client</th><th>State</th><th>Location</th><th>Seen</th><th>Current online</th><th>Session</th><th>Action</th></tr></thead>
              <tbody id="devices"></tbody>
            </table>
          </div>
          <div class="pager">
            <button id="device-prev" type="button">Previous</button>
            <span id="device-page-info">0-0 of 0</span>
            <button id="device-next" type="button">Next</button>
          </div>
        </section>
        <section class="panel">
          <div class="toolbar">
            <h2>Active Sessions</h2>
            <div class="actions">
              <input id="session-search" placeholder="Search session or user">
              <select id="session-limit" aria-label="Sessions per page">
                <option value="50">50 / page</option>
                <option value="100">100 / page</option>
                <option value="200">200 / page</option>
              </select>
            </div>
          </div>
          <div class="table-wrap">
            <table class="sessions-table">
              <thead><tr><th>Transmission</th><th>Participants</th><th>Action</th></tr></thead>
              <tbody id="sessions"></tbody>
            </table>
          </div>
          <div class="pager">
            <button id="session-prev" type="button">Previous</button>
            <span id="session-page-info">0-0 of 0</span>
            <button id="session-next" type="button">Next</button>
          </div>
        </section>
      </div>
    </section>
  </main>
  <script>
    const loginView = document.getElementById('login-view');
    const dashboardView = document.getElementById('dashboard-view');
    const logoutButton = document.getElementById('logout');
    const state = {
      devices: {
        limit: 50,
        offset: 0,
        total: 0,
        search: '',
        filter: 'online',
        sort: 'status',
        order: 'desc'
      },
      sessions: {limit: 50, offset: 0, total: 0, search: ''}
    };
    const searchTimers = {devices: null, sessions: null};
    const expandedDevices = new Set();
    let currentDevices = [];
    let statsTimer = null;
    let listTimer = null;
    let durationTimer = null;
    let listRefreshSerial = 0;
    let statsSnapshot = {
      onlineDuration: 0,
      onlineCount: 0,
      controlDuration: 0,
      controlledDuration: 0,
      activeConnections: 0,
      capturedAt: 0
    };
    const MAP_NS = 'http://www.w3.org/2000/svg';
    const GEO_COLORS = ['#edf2f7', '#d6e9f8', '#a9d3ee', '#6fb2d8', '#3288bd', '#1264a3'];
    const CHINA_PROVINCES = [
      {key: 'xinjiang', label: '新疆', d: 'M38 92 L170 70 L210 130 L185 205 L95 215 L45 172 Z', lx: 118, ly: 145},
      {key: 'tibet', label: '西藏', d: 'M58 236 L205 215 L260 275 L230 345 L120 352 L45 310 Z', lx: 148, ly: 292},
      {key: 'qinghai', label: '青海', d: 'M200 195 L300 190 L315 255 L260 275 L205 215 Z', lx: 260, ly: 230},
      {key: 'gansu', label: '甘肃', d: 'M300 145 L365 165 L350 220 L315 255 L300 190 L215 180 L210 130 Z', lx: 304, ly: 184},
      {key: 'inner_mongolia', label: '内蒙古', d: 'M170 70 L310 55 L455 75 L535 115 L510 155 L390 135 L300 145 L210 130 Z', lx: 350, ly: 105},
      {key: 'heilongjiang', label: '黑龙江', d: 'M490 35 L580 55 L590 120 L535 115 L455 75 Z', lx: 535, ly: 78},
      {key: 'jilin', label: '吉林', d: 'M510 120 L580 125 L575 170 L515 165 Z', lx: 545, ly: 147},
      {key: 'liaoning', label: '辽宁', d: 'M475 155 L545 170 L520 215 L465 190 Z', lx: 505, ly: 185},
      {key: 'beijing', label: '北京', d: 'M424 178 L442 178 L445 196 L428 202 L418 190 Z', lx: 432, ly: 190},
      {key: 'tianjin', label: '天津', d: 'M443 195 L460 200 L457 218 L438 216 Z', lx: 449, ly: 211},
      {key: 'hebei', label: '河北', d: 'M405 165 L465 190 L455 245 L395 235 L380 190 Z', lx: 418, ly: 220},
      {key: 'shanxi', label: '山西', d: 'M355 175 L395 175 L395 235 L355 240 L340 205 Z', lx: 370, ly: 212},
      {key: 'ningxia', label: '宁夏', d: 'M315 190 L350 205 L340 240 L305 230 Z', lx: 326, ly: 218},
      {key: 'shaanxi', label: '陕西', d: 'M320 230 L365 245 L350 310 L305 310 L290 265 Z', lx: 325, ly: 275},
      {key: 'shandong', label: '山东', d: 'M455 235 L535 245 L525 290 L455 285 Z', lx: 494, ly: 266},
      {key: 'henan', label: '河南', d: 'M365 245 L455 245 L455 300 L390 310 L350 280 Z', lx: 402, ly: 280},
      {key: 'jiangsu', label: '江苏', d: 'M495 295 L555 300 L555 360 L500 350 L475 315 Z', lx: 520, ly: 326},
      {key: 'anhui', label: '安徽', d: 'M440 305 L495 295 L500 350 L455 375 L425 345 Z', lx: 463, ly: 338},
      {key: 'shanghai', label: '上海', d: 'M555 348 L574 352 L570 372 L552 370 Z', lx: 562, ly: 365},
      {key: 'hubei', label: '湖北', d: 'M360 315 L425 310 L430 365 L370 380 L335 350 Z', lx: 382, ly: 347},
      {key: 'hunan', label: '湖南', d: 'M370 380 L430 365 L440 425 L385 445 L340 405 Z', lx: 390, ly: 410},
      {key: 'jiangxi', label: '江西', d: 'M440 375 L490 410 L470 460 L420 445 L400 405 Z', lx: 444, ly: 420},
      {key: 'zhejiang', label: '浙江', d: 'M500 360 L555 360 L545 420 L490 410 L470 375 Z', lx: 514, ly: 392},
      {key: 'fujian', label: '福建', d: 'M470 460 L520 430 L540 485 L500 520 L455 495 Z', lx: 499, ly: 478},
      {key: 'taiwan', label: '台湾', d: 'M565 455 C586 478 586 520 565 542 C545 520 545 478 565 455 Z', lx: 566, ly: 503},
      {key: 'sichuan', label: '四川', d: 'M250 305 L335 310 L340 405 L285 435 L220 390 L230 345 Z', lx: 282, ly: 365},
      {key: 'chongqing', label: '重庆', d: 'M335 350 L370 380 L340 405 L315 375 Z', lx: 342, ly: 380},
      {key: 'guizhou', label: '贵州', d: 'M285 435 L340 405 L365 455 L290 445 L260 470 Z', lx: 310, ly: 440},
      {key: 'yunnan', label: '云南', d: 'M205 425 L285 435 L260 510 L190 520 L150 465 Z', lx: 222, ly: 475},
      {key: 'guangxi', label: '广西', d: 'M290 445 L365 455 L335 500 L270 510 L240 470 Z', lx: 304, ly: 478},
      {key: 'guangdong', label: '广东', d: 'M365 455 L455 495 L500 520 L470 560 L380 545 L335 500 Z', lx: 417, ly: 513},
      {key: 'hainan', label: '海南', d: 'M385 578 C398 565 423 567 435 582 C422 600 398 601 385 578 Z', lx: 410, ly: 586},
      {key: 'hongkong', label: '香港', d: 'M470 548 C482 538 496 545 494 558 C482 568 471 561 470 548 Z', lx: 484, ly: 556},
      {key: 'macau', label: '澳门', d: 'M450 546 C459 538 468 543 467 554 C458 562 449 556 450 546 Z', lx: 458, ly: 552}
    ];
    let geoMapReady = false;
    let geoProvinceCounts = new Map();
    let geoTotalUsers = 0;

    function showDashboard() {
      loginView.classList.add('hidden');
      dashboardView.classList.remove('hidden');
      logoutButton.classList.remove('hidden');
      refreshStats();
      refreshLists();
      if (!statsTimer) statsTimer = setInterval(refreshStats, 5000);
      if (!listTimer) listTimer = setInterval(refreshLists, 5000);
      if (!durationTimer) durationTimer = setInterval(updateLiveDurations, 1000);
    }

    function showLogin(message) {
      dashboardView.classList.add('hidden');
      loginView.classList.remove('hidden');
      logoutButton.classList.add('hidden');
      if (statsTimer) clearInterval(statsTimer);
      statsTimer = null;
      if (listTimer) clearInterval(listTimer);
      listTimer = null;
      if (durationTimer) clearInterval(durationTimer);
      durationTimer = null;
      document.getElementById('login-error').textContent = message || '';
    }

    async function login(event) {
      event.preventDefault();
      const body = JSON.stringify({
        username: document.getElementById('username').value,
        password: document.getElementById('password').value
      });
      const response = await fetch('/api/admin/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        credentials: 'same-origin',
        body
      });
      if (response.ok) showDashboard();
      else showLogin('Invalid username or password');
    }

    async function logout() {
      await fetch('/api/admin/logout', {method: 'POST', credentials: 'same-origin'});
      showLogin('');
    }

    function formatTime(value) {
      if (!value) return '-';
      return new Date(value * 1000).toLocaleString();
    }

    function formatDuration(value) {
      let seconds = Number(value) || 0;
      if (seconds < 0) seconds = 0;
      const days = Math.floor(seconds / 86400);
      seconds %= 86400;
      const hours = Math.floor(seconds / 3600);
      seconds %= 3600;
      const minutes = Math.floor(seconds / 60);
      seconds = Math.floor(seconds % 60);
      if (days > 0) return `${days}d ${hours}h`;
      if (hours > 0) return `${hours}h ${minutes}m`;
      if (minutes > 0) return `${minutes}m ${seconds}s`;
      return `${seconds}s`;
    }

    function appendEmptyRow(body, colSpan) {
      const row = document.createElement('tr');
      const cell = document.createElement('td');
      cell.className = 'empty';
      cell.colSpan = colSpan;
      cell.textContent = 'No records';
      row.appendChild(cell);
      body.appendChild(row);
    }

    function appendText(parent, tag, value, className) {
      const element = document.createElement(tag);
      if (className) element.className = className;
      element.textContent = value;
      parent.appendChild(element);
      return element;
    }

    function labelCell(cell, label) {
      cell.dataset.label = label;
      return cell;
    }

    function appendBadge(parent, value, className) {
      return appendText(parent, 'span', value, `badge ${className}`);
    }

    function formatPercent(count, total) {
      if (!total) return '0%';
      return `${((Number(count) || 0) * 100 / total).toFixed(1)}%`;
    }

    function geoColor(count, maxCount) {
      const value = Number(count) || 0;
      if (value <= 0 || maxCount <= 0) return GEO_COLORS[0];
      const index = Math.max(1, Math.ceil((value / maxCount) * (GEO_COLORS.length - 1)));
      return GEO_COLORS[Math.min(index, GEO_COLORS.length - 1)];
    }

    function provinceTooltipText(province) {
      const count = geoProvinceCounts.get(province.key) || 0;
      return `${province.label}: ${count} (${formatPercent(count, geoTotalUsers)})`;
    }

    function moveGeoTooltip(event) {
      const wrap = document.querySelector('.china-map-wrap');
      const tooltip = document.getElementById('geo-tooltip');
      if (!wrap || !tooltip) return;
      const rect = wrap.getBoundingClientRect();
      if (typeof event.clientX !== 'number' || typeof event.clientY !== 'number') {
        tooltip.style.left = '10px';
        tooltip.style.top = '10px';
        return;
      }
      const left = Math.max(0, Math.min(event.clientX - rect.left, rect.width - 220));
      const top = Math.max(0, Math.min(event.clientY - rect.top, rect.height - 54));
      tooltip.style.left = `${left}px`;
      tooltip.style.top = `${top}px`;
    }

    function showGeoTooltip(event, province) {
      const tooltip = document.getElementById('geo-tooltip');
      tooltip.textContent = provinceTooltipText(province);
      tooltip.classList.add('visible');
      moveGeoTooltip(event);
    }

    function hideGeoTooltip() {
      document.getElementById('geo-tooltip').classList.remove('visible');
    }

    function ensureChinaMap() {
      if (geoMapReady) return;
      const svg = document.getElementById('china-map');
      CHINA_PROVINCES.forEach(province => {
        const path = document.createElementNS(MAP_NS, 'path');
        path.id = `geo-province-${province.key}`;
        path.classList.add('province');
        path.setAttribute('d', province.d);
        path.setAttribute('fill', GEO_COLORS[0]);
        path.setAttribute('tabindex', '0');
        path.setAttribute('role', 'img');
        path.setAttribute('aria-label', provinceTooltipText(province));
        path.addEventListener('mouseenter', event => showGeoTooltip(event, province));
        path.addEventListener('mousemove', moveGeoTooltip);
        path.addEventListener('mouseleave', hideGeoTooltip);
        path.addEventListener('focus', event => showGeoTooltip(event, province));
        path.addEventListener('blur', hideGeoTooltip);
        svg.appendChild(path);

        const label = document.createElementNS(MAP_NS, 'text');
        label.setAttribute('x', province.lx);
        label.setAttribute('y', province.ly);
        label.textContent = province.label;
        svg.appendChild(label);
      });
      geoMapReady = true;
    }

    function renderGeoDistribution(distribution) {
      ensureChinaMap();
      const data = distribution || {total_count: 0, foreign_count: 0, unknown_count: 0, provinces: []};
      geoTotalUsers = Number(data.total_count) || 0;
      geoProvinceCounts = new Map();
      let maxProvinceCount = 0;
      (Array.isArray(data.provinces) ? data.provinces : []).forEach(item => {
        const count = Number(item.count) || 0;
        geoProvinceCounts.set(item.province, count);
        if (count > maxProvinceCount) maxProvinceCount = count;
      });

      CHINA_PROVINCES.forEach(province => {
        const count = geoProvinceCounts.get(province.key) || 0;
        const path = document.getElementById(`geo-province-${province.key}`);
        if (!path) return;
        path.setAttribute('fill', geoColor(count, maxProvinceCount));
        path.setAttribute('aria-label', provinceTooltipText(province));
      });

      const foreignCount = Number(data.foreign_count) || 0;
      const unknownCount = Number(data.unknown_count) || 0;
      document.getElementById('geo-total').textContent = geoTotalUsers;
      document.getElementById('geo-foreign-count').textContent = foreignCount;
      document.getElementById('geo-foreign-percent').textContent = formatPercent(foreignCount, geoTotalUsers);
      document.getElementById('geo-unknown-count').textContent = unknownCount;
      document.getElementById('geo-unknown-percent').textContent = formatPercent(unknownCount, geoTotalUsers);

      const topList = document.getElementById('geo-top-provinces');
      topList.textContent = '';
      const topProvinces = (Array.isArray(data.provinces) ? data.provinces : []).slice(0, 5);
      if (!topProvinces.length) {
        const item = document.createElement('li');
        item.textContent = '-';
        topList.appendChild(item);
        return;
      }
      topProvinces.forEach(item => {
        const province = CHINA_PROVINCES.find(candidate => candidate.key === item.province);
        const count = Number(item.count) || 0;
        const li = document.createElement('li');
        li.textContent = `${province ? province.label : item.province}: ${count} (${formatPercent(count, geoTotalUsers)})`;
        topList.appendChild(li);
      });
    }

    function setDurationDataset(cell, kind, device, base, capturedAt, running, rate) {
      const isRunning = typeof running === 'boolean' ? running : device.online;
      cell.dataset.duration = kind;
      cell.dataset.online = device.online ? '1' : '0';
      cell.dataset.running = isRunning ? '1' : '0';
      cell.dataset.base = String(base || 0);
      cell.dataset.capturedAt = String(capturedAt);
      cell.dataset.rate = String(rate || 1);
    }

    function sessionSummary(device) {
      const targets = Array.isArray(device.active_control_targets) ? device.active_control_targets : [];
      const controlledBy = Array.isArray(device.active_controlled_by) ? device.active_controlled_by : [];
      const controlling = Number(device.active_control_count) || targets.length;
      const controlled = Number(device.active_controlled_count) || controlledBy.length;
      const parts = [];
      if (controlling > 0) parts.push(`controlling ${controlling}`);
      if (controlled > 0) parts.push(`controlled by ${controlled}`);
      return parts.join(', ') || '-';
    }

    function peerList(value) {
      return Array.isArray(value) && value.length ? value.join(', ') : '-';
    }

    function locationLabel(device) {
      if (device.geo_location) return device.geo_location;
      if (device.client_ip) return 'Unknown';
      return '-';
    }

    function activeDuration(value, activeCount) {
      return Number(activeCount) > 0 ? formatDuration(value) : '-';
    }

    function appendDetailItem(parent, label, value, className, dataset) {
      const item = document.createElement('div');
      appendText(item, 'span', label);
      const strong = appendText(item, 'strong', value, className);
      if (dataset) {
        Object.keys(dataset).forEach(key => {
          strong.dataset[key] = dataset[key];
        });
      }
      parent.appendChild(item);
      return strong;
    }

    function renderDevices(devices) {
      currentDevices = devices;
      const body = document.getElementById('devices');
      body.textContent = '';
      if (!devices.length) {
        appendEmptyRow(body, 7);
        return;
      }
      const capturedAt = Math.floor(Date.now() / 1000);
      devices.forEach(device => {
        const activeSessions = Number(device.active_session_count) || 0;
        const isExpanded = expandedDevices.has(device.id);
        const row = document.createElement('tr');
        row.className = isExpanded ? 'device-row expanded' : 'device-row';
        const clientCell = document.createElement('td');
        labelCell(clientCell, 'Client');
        appendText(clientCell, 'div', device.id, 'device-id');
        appendText(clientCell, 'span', device.kind === 'web' ? 'web client' : 'device', 'subline');
        row.appendChild(clientCell);

        const statusCell = document.createElement('td');
        labelCell(statusCell, 'State');
        appendBadge(statusCell, device.online ? 'online' : 'offline',
          device.online ? 'online' : 'offline');
        if (activeSessions > 0) appendBadge(statusCell, 'remote', 'active');
        row.appendChild(statusCell);

        const locationCell = document.createElement('td');
        labelCell(locationCell, 'Location');
        appendText(locationCell, 'div', locationLabel(device));
        appendText(locationCell, 'span', device.client_ip || '-', 'muted');
        row.appendChild(locationCell);

        const timeCell = document.createElement('td');
        labelCell(timeCell, 'Seen');
        appendText(timeCell, 'div', formatTime(device.online ? device.online_since : device.updated_at));
        appendText(timeCell, 'span', device.online ? 'online since' : 'last online', 'muted');
        row.appendChild(timeCell);

        const currentCell = appendText(row, 'td', device.online ? formatDuration(device.online_duration_seconds) : '-');
        labelCell(currentCell, 'Current online');
        setDurationDataset(currentCell, 'current', device, device.online_duration_seconds, capturedAt);
        labelCell(appendText(row, 'td', sessionSummary(device)), 'Session');

        const actionCell = document.createElement('td');
        labelCell(actionCell, 'Action');
        const detailButton = document.createElement('button');
        detailButton.type = 'button';
        detailButton.textContent = isExpanded ? 'Hide' : 'Details';
        detailButton.addEventListener('click', () => {
          if (expandedDevices.has(device.id)) expandedDevices.delete(device.id);
          else expandedDevices.add(device.id);
          renderDevices(currentDevices);
        });
        actionCell.appendChild(detailButton);
        row.appendChild(actionCell);
        body.appendChild(row);

        if (isExpanded) {
          const detailsRow = document.createElement('tr');
          detailsRow.className = 'details-row';
          const detailsCell = document.createElement('td');
          detailsCell.colSpan = 7;
          const details = document.createElement('div');
          details.className = 'detail-grid';
          const currentOnline = appendDetailItem(
            details, 'Current online',
            device.online ? formatDuration(device.online_duration_seconds) : '-');
          setDurationDataset(currentOnline, 'current-online', device,
            device.online_duration_seconds, capturedAt);
          const totalOnline = appendDetailItem(
            details, 'Total online', formatDuration(device.total_online_seconds));
          setDurationDataset(totalOnline, 'total', device, device.total_online_seconds, capturedAt);
          const activeControlCount = Number(device.active_control_count) || 0;
          const activeControlledCount = Number(device.active_controlled_count) || 0;
          const currentControl = appendDetailItem(
            details, 'Current control',
            activeDuration(device.current_control_seconds, activeControlCount));
          setDurationDataset(currentControl, 'current-control', device,
            device.current_control_seconds, capturedAt,
            activeControlCount > 0, activeControlCount);
          const totalControl = appendDetailItem(
            details, 'Total control', formatDuration(device.total_control_seconds));
          setDurationDataset(totalControl, 'total-control', device,
            device.total_control_seconds, capturedAt,
            activeControlCount > 0, activeControlCount);
          const currentControlled = appendDetailItem(
            details, 'Current controlled',
            activeDuration(device.current_controlled_seconds, activeControlledCount));
          setDurationDataset(currentControlled, 'current-controlled', device,
            device.current_controlled_seconds, capturedAt,
            activeControlledCount > 0, activeControlledCount);
          const totalControlled = appendDetailItem(
            details, 'Total controlled', formatDuration(device.total_controlled_seconds));
          setDurationDataset(totalControlled, 'total-controlled', device,
            device.total_controlled_seconds, capturedAt,
            activeControlledCount > 0, activeControlledCount);
          appendDetailItem(details, 'Location', locationLabel(device));
          appendDetailItem(details, 'Client IP', device.client_ip || '-');
          appendDetailItem(details, 'City', device.geo_city || '-');
          appendDetailItem(details, 'Region', device.geo_region || '-');
          appendDetailItem(details, 'Country', device.geo_country || '-');
          appendDetailItem(details, 'Online since', formatTime(device.online_since));
          appendDetailItem(details, 'Last online', formatTime(device.online ? 0 : device.updated_at));
          appendDetailItem(details, 'Active session', sessionSummary(device));
          appendDetailItem(details, 'Controlling', peerList(device.active_control_targets), 'peer-list');
          appendDetailItem(details, 'Controlled by', peerList(device.active_controlled_by), 'peer-list');
          detailsCell.appendChild(details);
          detailsRow.appendChild(detailsCell);
          body.appendChild(detailsRow);
        }
      });
      updateLiveDurations();
    }

    function renderSessions(sessions) {
      const body = document.getElementById('sessions');
      body.textContent = '';
      if (!sessions.length) {
        appendEmptyRow(body, 3);
        return;
      }
      sessions.forEach(session => {
        const guests = session.guest_ids.join(', ') || '-';
        const row = document.createElement('tr');
        row.className = 'session-row';
        const transmissionCell = document.createElement('td');
        labelCell(transmissionCell, 'Transmission');
        appendText(transmissionCell, 'div', session.transmission_id);
        appendText(transmissionCell, 'span', `host ${session.host_id}`, 'muted');
        row.appendChild(transmissionCell);

        const participantsCell = document.createElement('td');
        labelCell(participantsCell, 'Participants');
        appendText(participantsCell, 'div', session.participant_count);
        appendText(participantsCell, 'span', guests, 'muted');
        row.appendChild(participantsCell);

        const actionCell = document.createElement('td');
        labelCell(actionCell, 'Action');
        const button = document.createElement('button');
        button.className = 'danger';
        button.textContent = 'Disconnect';
        button.dataset.id = session.transmission_id;
        button.dataset.host = session.host_id;
        button.addEventListener('click', () => disconnectSession(button.dataset.id, button.dataset.host, button));
        actionCell.appendChild(button);
        row.appendChild(actionCell);
        body.appendChild(row);
      });
    }

    function buildOverviewUrl() {
      const params = new URLSearchParams();
      params.set('device_limit', state.devices.limit);
      params.set('device_offset', state.devices.offset);
      params.set('device_filter', state.devices.filter);
      params.set('device_sort', state.devices.sort);
      params.set('device_order', state.devices.order);
      params.set('session_limit', state.sessions.limit);
      params.set('session_offset', state.sessions.offset);
      if (state.devices.search) params.set('device_search', state.devices.search);
      if (state.sessions.search) params.set('session_search', state.sessions.search);
      return `/api/admin/overview?${params.toString()}`;
    }

    function syncPage(pageState, pageData) {
      if (!pageData) return false;
      pageState.limit = pageData.limit;
      pageState.offset = pageData.offset;
      pageState.total = pageData.total;
      if (pageState.total > 0 && pageState.offset >= pageState.total && pageState.limit > 0) {
        pageState.offset = Math.floor((pageState.total - 1) / pageState.limit) * pageState.limit;
        return true;
      }
      return false;
    }

    function updatePager(kind) {
      const page = state[kind];
      const prefix = kind === 'devices' ? 'device' : 'session';
      const start = page.total === 0 ? 0 : Math.min(page.offset + 1, page.total);
      const end = Math.min(page.offset + page.limit, page.total);
      document.getElementById(`${prefix}-page-info`).textContent = `${start}-${end} of ${page.total}`;
      document.getElementById(`${prefix}-prev`).disabled = page.offset === 0;
      document.getElementById(`${prefix}-next`).disabled = page.offset + page.limit >= page.total;
      document.getElementById(`${prefix}-limit`).value = String(page.limit);
      if (kind === 'devices') {
        document.getElementById('device-sort').value = page.sort;
        document.getElementById('device-order').textContent = page.order === 'asc' ? 'ASC' : 'DESC';
        document.getElementById('device-order').title = page.order === 'asc' ? 'Ascending' : 'Descending';
      }
    }

    function applyDeviceCounts(counts) {
      if (counts) {
        ['all', 'online', 'offline', 'active', 'web'].forEach(filter => {
          const count = Number(counts[filter]) || 0;
          const countElement = document.getElementById(`device-count-${filter}`);
          if (countElement) countElement.textContent = count;
        });
      }
      document.querySelectorAll('[data-device-filter]').forEach(button => {
        button.classList.toggle('active', button.dataset.deviceFilter === state.devices.filter);
        button.setAttribute('aria-selected', button.dataset.deviceFilter === state.devices.filter ? 'true' : 'false');
      });
    }

    function applyStats(stats) {
      document.getElementById('metric-devices').textContent = stats.online_device_count;
      document.getElementById('metric-web').textContent = stats.online_web_client_count;
      document.getElementById('metric-sessions').textContent = stats.active_connection_count;
      document.getElementById('metric-duration').textContent = formatDuration(stats.total_online_seconds);
      document.getElementById('metric-control').textContent = formatDuration(stats.total_control_seconds);
      document.getElementById('metric-controlled').textContent = formatDuration(stats.total_controlled_seconds);
      document.getElementById('last-refresh').textContent = new Date().toLocaleTimeString();
      statsSnapshot = {
        onlineDuration: Number(stats.total_online_seconds) || 0,
        onlineCount: Number(stats.online_device_count) || 0,
        controlDuration: Number(stats.total_control_seconds) || 0,
        controlledDuration: Number(stats.total_controlled_seconds) || 0,
        activeConnections: Number(stats.active_connection_count) || 0,
        capturedAt: Math.floor(Date.now() / 1000)
      };
    }

    function updateLiveDurations() {
      const now = Math.floor(Date.now() / 1000);
      document.querySelectorAll('[data-duration]').forEach(cell => {
        if (cell.dataset.running !== '1') return;
        const base = Number(cell.dataset.base) || 0;
        const capturedAt = Number(cell.dataset.capturedAt) || now;
        const rate = Number(cell.dataset.rate) || 1;
        cell.textContent = formatDuration(base + rate * (now - capturedAt));
      });
      if (statsSnapshot.capturedAt > 0) {
        const elapsed = now - statsSnapshot.capturedAt;
        document.getElementById('metric-duration').textContent =
          formatDuration(statsSnapshot.onlineDuration +
            statsSnapshot.onlineCount * elapsed);
        document.getElementById('metric-control').textContent =
          formatDuration(statsSnapshot.controlDuration +
            statsSnapshot.activeConnections * elapsed);
        document.getElementById('metric-controlled').textContent =
          formatDuration(statsSnapshot.controlledDuration +
            statsSnapshot.activeConnections * elapsed);
      }
    }

    async function refreshStats() {
      let response;
      try {
        response = await fetch('/api/admin/stats', {credentials: 'same-origin'});
      } catch (_) {
        document.getElementById('refresh-error').textContent = 'Connection error';
        return false;
      }
      if (response.status === 401) {
        showLogin('');
        return false;
      }
      if (!response.ok) {
        document.getElementById('refresh-error').textContent = 'Connection error';
        return false;
      }
      const data = await response.json();
      document.getElementById('refresh-error').textContent = '';
      applyStats(data.stats);
      return true;
    }

    async function refreshLists() {
      const serial = ++listRefreshSerial;
      const refreshButton = document.getElementById('list-refresh');
      refreshButton.disabled = true;
      let response;
      try {
        response = await fetch(buildOverviewUrl(), {credentials: 'same-origin'});
      } catch (_) {
        document.getElementById('refresh-error').textContent = 'Connection error';
        refreshButton.disabled = false;
        return false;
      }
      if (response.status === 401) {
        showLogin('');
        refreshButton.disabled = false;
        return false;
      }
      if (!response.ok) {
        document.getElementById('refresh-error').textContent = 'Connection error';
        refreshButton.disabled = false;
        return false;
      }
      const data = await response.json();
      if (serial !== listRefreshSerial) {
        refreshButton.disabled = false;
        return true;
      }
      document.getElementById('refresh-error').textContent = '';
      applyStats(data.stats);
      const reloadDevices = syncPage(state.devices, data.devices_page);
      const reloadSessions = syncPage(state.sessions, data.sessions_page);
      if (reloadDevices || reloadSessions) {
        refreshButton.disabled = false;
        return refreshLists();
      }
      applyDeviceCounts(data.device_counts);
      renderGeoDistribution(data.geo_distribution);
      renderDevices(data.devices || []);
      renderSessions(data.sessions || []);
      updatePager('devices');
      updatePager('sessions');
      refreshButton.disabled = false;
      return true;
    }

    async function disconnectSession(id, host, button) {
      if (!confirm(`Disconnect session ${id} for host ${host}? Devices stay online.`)) return;
      button.disabled = true;
      const response = await fetch(`/api/admin/sessions/${encodeURIComponent(id)}/disconnect`, {
        method: 'POST',
        credentials: 'same-origin'
      });
      button.disabled = false;
      if (response.ok) {
        refreshStats();
        refreshLists();
      }
      else document.getElementById('refresh-error').textContent = 'Failed to disconnect session';
    }

    document.getElementById('login-form').addEventListener('submit', login);
    document.getElementById('logout').addEventListener('click', logout);
    document.getElementById('device-search').addEventListener('input', (event) => {
      state.devices.search = event.target.value.trim();
      state.devices.offset = 0;
      clearTimeout(searchTimers.devices);
      searchTimers.devices = setTimeout(refreshLists, 250);
    });
    document.querySelectorAll('[data-device-filter]').forEach(button => {
      button.addEventListener('click', () => {
        state.devices.filter = button.dataset.deviceFilter;
        state.devices.offset = 0;
        expandedDevices.clear();
        applyDeviceCounts();
        refreshLists();
      });
    });
    document.getElementById('device-sort').addEventListener('change', (event) => {
      state.devices.sort = event.target.value;
      state.devices.offset = 0;
      refreshLists();
    });
    document.getElementById('device-order').addEventListener('click', () => {
      state.devices.order = state.devices.order === 'asc' ? 'desc' : 'asc';
      state.devices.offset = 0;
      refreshLists();
    });
    document.getElementById('session-search').addEventListener('input', (event) => {
      state.sessions.search = event.target.value.trim();
      state.sessions.offset = 0;
      clearTimeout(searchTimers.sessions);
      searchTimers.sessions = setTimeout(refreshLists, 250);
    });
    document.getElementById('device-limit').addEventListener('change', (event) => {
      state.devices.limit = Number(event.target.value);
      state.devices.offset = 0;
      refreshLists();
    });
    document.getElementById('session-limit').addEventListener('change', (event) => {
      state.sessions.limit = Number(event.target.value);
      state.sessions.offset = 0;
      refreshLists();
    });
    document.getElementById('device-prev').addEventListener('click', () => {
      state.devices.offset = Math.max(0, state.devices.offset - state.devices.limit);
      refreshLists();
    });
    document.getElementById('device-next').addEventListener('click', () => {
      if (state.devices.offset + state.devices.limit < state.devices.total) {
        state.devices.offset += state.devices.limit;
        refreshLists();
      }
    });
    document.getElementById('session-prev').addEventListener('click', () => {
      state.sessions.offset = Math.max(0, state.sessions.offset - state.sessions.limit);
      refreshLists();
    });
    document.getElementById('session-next').addEventListener('click', () => {
      if (state.sessions.offset + state.sessions.limit < state.sessions.total) {
        state.sessions.offset += state.sessions.limit;
        refreshLists();
      }
    });
    document.getElementById('list-refresh').addEventListener('click', refreshLists);
    applyDeviceCounts();
    renderGeoDistribution();
    refreshStats().then((ok) => {
      if (ok) showDashboard();
      else showLogin('');
    }).catch(() => showLogin(''));
  </script>
</body>
</html>)HTML";

std::string JsonContentType() { return "application/json; charset=utf-8"; }

}  // namespace

AdminController::AdminController(
    AdminAuth* auth, PresenceManager* presence,
    std::shared_ptr<TransmissionManager> transmission, DeviceDBManager* db,
    std::function<void(const std::string&, nlohmann::json)> send_to_user)
    : auth_(auth),
      presence_(presence),
      transmission_(std::move(transmission)),
      db_(db),
      send_to_user_(std::move(send_to_user)) {}

bool AdminController::IsAdminRoute(const std::string& resource) {
  std::string path = ResourcePath(resource);
  return path == "/admin" || path == "/api/admin" ||
         path.rfind("/api/admin/", 0) == 0;
}

std::string AdminController::ExtractDisconnectTransmissionId(
    const std::string& resource) {
  std::string path = ResourcePath(resource);
  if (path.rfind(kDisconnectPrefix, 0) != 0) {
    return "";
  }

  std::string tail = path.substr(std::string(kDisconnectPrefix).size());
  if (tail.size() <= std::string(kDisconnectSuffix).size()) {
    return "";
  }

  size_t suffix_pos = tail.rfind(kDisconnectSuffix);
  if (suffix_pos == std::string::npos ||
      suffix_pos + std::string(kDisconnectSuffix).size() != tail.size()) {
    return "";
  }

  return tail.substr(0, suffix_pos);
}

AdminHttpResponse AdminController::Handle(const AdminHttpRequest& request) {
  std::string path = ResourcePath(request.resource);
  if (path == "/admin") {
    return HandleAdminPage();
  }

  if (!auth_ || !auth_->IsEnabled()) {
    return ErrorResponse(503, "admin_disabled");
  }

  if (path == "/api/admin/login") {
    return HandleLogin(request);
  }

  if (!IsAuthorized(request)) {
    return ErrorResponse(401, "unauthorized");
  }

  if (path == "/api/admin/logout") {
    return HandleLogout(request);
  }
  if (path == "/api/admin/stats") {
    return HandleStats(request);
  }
  if (path == "/api/admin/overview") {
    return HandleOverview(request);
  }
  if (!ExtractDisconnectTransmissionId(path).empty()) {
    return HandleDisconnect(request);
  }

  return ErrorResponse(404, "not_found");
}

AdminHttpResponse AdminController::HandleAdminPage() {
  if (!auth_ || !auth_->IsEnabled()) {
    return HtmlResponse(
        200,
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>CrossDesk "
        "Admin</title></head><body><h1>CrossDesk Admin</h1><p>Admin "
        "dashboard is not enabled. Set ADMIN_USERNAME and ADMIN_PASSWORD to "
        "enable it.</p></body></html>");
  }
  return HtmlResponse(200, kAdminHtml);
}

AdminHttpResponse AdminController::HandleLogin(
    const AdminHttpRequest& request) {
  if (request.method != "POST") {
    return ErrorResponse(405, "method_not_allowed");
  }

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return ErrorResponse(400, "invalid_json");
  }

  if (!body.contains("username") || !body["username"].is_string() ||
      !body.contains("password") || !body["password"].is_string()) {
    return ErrorResponse(400, "invalid_json");
  }

  auto token = auth_->Login(body["username"].get<std::string>(),
                            body["password"].get<std::string>());
  if (!token.has_value()) {
    return ErrorResponse(401, "unauthorized");
  }

  AdminHttpResponse response = JsonResponse(200, {{"ok", true}});
  response.headers.push_back({"Set-Cookie", auth_->BuildSessionCookie(*token)});
  return response;
}

AdminHttpResponse AdminController::HandleLogout(
    const AdminHttpRequest& request) {
  if (request.method != "POST") {
    return ErrorResponse(405, "method_not_allowed");
  }

  std::string token = AdminAuth::ExtractCookie(request.cookie, kSessionCookieName);
  if (!token.empty()) {
    auth_->Logout(token);
  }

  AdminHttpResponse response = JsonResponse(200, {{"ok", true}});
  response.headers.push_back({"Set-Cookie", auth_->BuildExpiredCookie()});
  return response;
}

AdminHttpResponse AdminController::HandleStats(
    const AdminHttpRequest& request) {
  if (request.method != "GET") {
    return ErrorResponse(405, "method_not_allowed");
  }

  size_t online_device_fallback =
      !presence_ && db_ ? static_cast<size_t>(db_->CountOnlineDevices()) : 0;
  return JsonResponse(200, {{"stats", BuildStats(online_device_fallback)}});
}

AdminHttpResponse AdminController::HandleOverview(
    const AdminHttpRequest& request) {
  if (request.method != "GET") {
    return ErrorResponse(405, "method_not_allowed");
  }

  auto params = ParseQueryParams(ResourceQuery(request.resource));
  size_t device_limit =
      QuerySizeParam(params, "device_limit", kDefaultPageLimit, kMaxPageLimit);
  size_t device_offset =
      QuerySizeParam(params, "device_offset", 0, static_cast<size_t>(-1));
  std::string device_search = QueryStringParam(params, "device_search");
  std::string device_filter =
      QueryStringParam(params, "device_filter", "online");
  std::string device_sort =
      QueryStringParam(params, "device_sort", "status");
  std::string device_order =
      QueryStringParam(params, "device_order", "desc");
  size_t session_limit =
      QuerySizeParam(params, "session_limit", kDefaultPageLimit, kMaxPageLimit);
  size_t session_offset =
      QuerySizeParam(params, "session_offset", 0, static_cast<size_t>(-1));
  std::string session_search = QueryStringParam(params, "session_search");

  nlohmann::json devices = nlohmann::json::array();
  size_t devices_total = 0;
  nlohmann::json device_counts = {{"all", 0},
                                  {"online", 0},
                                  {"offline", 0},
                                  {"active", 0},
                                  {"web", 0}};
  size_t online_device_fallback = 0;
  if (db_) {
    device_counts["all"] = db_->CountDevicePresence(device_search, "all");
    device_counts["online"] =
        db_->CountDevicePresence(device_search, "online");
    device_counts["offline"] =
        db_->CountDevicePresence(device_search, "offline");
    device_counts["active"] =
        db_->CountDevicePresence(device_search, "active");
    device_counts["web"] = db_->CountDevicePresence(device_search, "web");
    devices_total =
        static_cast<size_t>(db_->CountDevicePresence(device_search,
                                                     device_filter));
    online_device_fallback = static_cast<size_t>(db_->CountOnlineDevices());
    for (const auto& device :
         db_->ListDevicePresence(device_limit, device_offset, device_search,
                                 device_filter, device_sort, device_order)) {
      int64_t active_control_count = device.active_control_count;
      int64_t active_controlled_count = device.active_controlled_count;
      devices.push_back({{"id", device.device_id},
                         {"online", device.online},
                         {"kind", ClientKind(device.device_id)},
                         {"updated_at", device.updated_at},
                         {"last_online_at",
                          device.online ? 0 : device.updated_at},
                         {"online_since", device.online_since},
                         {"online_duration_seconds",
                          device.online_duration_seconds},
                         {"total_online_seconds",
                          device.total_online_seconds},
                         {"total_control_seconds",
                          device.total_control_seconds},
                         {"total_controlled_seconds",
                          device.total_controlled_seconds},
                         {"client_ip", device.client_ip},
                         {"geo_country", device.country},
                         {"geo_region", device.region},
                         {"geo_city", device.city},
                         {"geo_location", device.location},
                         {"current_control_seconds",
                          device.current_control_seconds},
                         {"current_controlled_seconds",
                          device.current_controlled_seconds},
                         {"active_control_count", active_control_count},
                         {"active_controlled_count",
                          active_controlled_count},
                         {"active_control_targets",
                          device.active_control_targets},
                         {"active_controlled_by",
                          device.active_controlled_by},
                         {"active_session_count",
                          active_control_count +
                              active_controlled_count}});
    }
  }

  nlohmann::json sessions = nlohmann::json::array();
  size_t sessions_total = 0;
  if (db_) {
    sessions_total =
        static_cast<size_t>(db_->CountRemoteControlTransmissions(
            session_search));
    for (const auto& session : db_->ListRemoteControlSessions(
             session_limit, session_offset, session_search)) {
      sessions.push_back({{"transmission_id", session.transmission_id},
                          {"host_id", session.host_id},
                          {"guest_ids", session.guest_ids},
                          {"participant_count", 1 + session.guest_ids.size()},
                          {"active", true}});
    }
  }
  if (sessions_total == 0 && transmission_) {
    for (const auto& snapshot : transmission_->GetTransmissionSnapshots(
             session_limit, session_offset, session_search, &sessions_total)) {
      sessions.push_back({{"transmission_id", snapshot.transmission_id},
                          {"host_id", snapshot.host_id},
                          {"guest_ids", snapshot.guest_ids},
                          {"participant_count", snapshot.participant_count},
                          {"active", snapshot.active}});
    }
  }

  nlohmann::json geo_distribution = {{"total_count", 0},
                                     {"foreign_count", 0},
                                     {"unknown_count", 0},
                                     {"provinces", nlohmann::json::array()}};
  if (db_) {
    ClientGeoDistribution distribution = db_->GetClientGeoDistribution();
    geo_distribution["total_count"] = distribution.total_count;
    geo_distribution["foreign_count"] = distribution.foreign_count;
    geo_distribution["unknown_count"] = distribution.unknown_count;
    for (const auto& province : distribution.provinces) {
      geo_distribution["provinces"].push_back(
          {{"province", province.province}, {"count", province.count}});
    }
  }

  nlohmann::json stats = BuildStats(online_device_fallback);

  nlohmann::json devices_page = {{"limit", device_limit},
                                 {"offset", device_offset},
                                 {"total", devices_total},
                                 {"search", device_search},
                                 {"filter", device_filter},
                                 {"sort", device_sort},
                                 {"order", device_order}};
  nlohmann::json sessions_page = {{"limit", session_limit},
                                  {"offset", session_offset},
                                  {"total", sessions_total},
                                  {"search", session_search}};

  return JsonResponse(200, {{"stats", stats},
                            {"devices", devices},
                            {"devices_page", devices_page},
                            {"device_counts", device_counts},
                            {"geo_distribution", geo_distribution},
                            {"sessions", sessions},
                            {"sessions_page", sessions_page}});
}

AdminHttpResponse AdminController::HandleDisconnect(
    const AdminHttpRequest& request) {
  if (request.method != "POST") {
    return ErrorResponse(405, "method_not_allowed");
  }

  std::string transmission_id =
      ExtractDisconnectTransmissionId(request.resource);
  if (transmission_id.empty()) {
    return ErrorResponse(404, "not_found");
  }

  bool existed = false;
  if (transmission_) {
    auto snapshots = transmission_->GetTransmissionSnapshots();
    for (const auto& snapshot : snapshots) {
      if (snapshot.transmission_id != transmission_id) {
        continue;
      }
      existed = true;
      nlohmann::json message = {{"type", "admin_disconnect_transmission"},
                                {"transmission_id", transmission_id}};
      if (send_to_user_) {
        send_to_user_(snapshot.host_id, message);
        for (const auto& guest_id : snapshot.guest_ids) {
          send_to_user_(guest_id, message);
        }
      }
      break;
    }
    transmission_->DisconnectTransmission(transmission_id);
  }

  bool persisted = false;
  if (db_) {
    for (const auto& session : db_->ListRemoteControlSessions(
             static_cast<size_t>(std::numeric_limits<int>::max()), 0,
             transmission_id)) {
      if (session.transmission_id == transmission_id) {
        persisted = true;
        break;
      }
    }
    if (persisted) {
      db_->EndRemoteControlTransmission(transmission_id);
    }
  }

  if (!existed && !persisted) {
    return JsonResponse(200, {{"ok", true}, {"already_closed", true}});
  }
  return JsonResponse(200, {{"ok", true}});
}

bool AdminController::IsAuthorized(const AdminHttpRequest& request) {
  if (!auth_) {
    return false;
  }
  std::string token = AdminAuth::ExtractCookie(request.cookie, kSessionCookieName);
  return auth_->ValidateSession(token);
}

nlohmann::json AdminController::BuildStats(size_t online_device_fallback) const {
  OnlineDurationStats duration_stats;
  if (db_) {
    duration_stats = db_->GetOnlineDurationStats();
  }
  size_t active_connection_count =
      transmission_ ? transmission_->GetActiveConnectionCount() : 0;
  if (db_) {
    active_connection_count = std::max(
        active_connection_count,
        static_cast<size_t>(db_->CountActiveRemoteControlConnections()));
  }

  return {{"online_device_count",
           presence_ ? presence_->GetOnlineDeviceCount()
                     : online_device_fallback},
          {"online_web_client_count",
           presence_ ? presence_->GetOnlineWebClientCount() : 0},
          {"active_connection_count", active_connection_count},
          {"online_duration_seconds",
           duration_stats.current_online_seconds},
          {"total_online_seconds", duration_stats.total_online_seconds},
          {"total_control_seconds", duration_stats.total_control_seconds},
          {"total_controlled_seconds",
           duration_stats.total_controlled_seconds}};
}

AdminHttpResponse AdminController::JsonResponse(
    int status, const nlohmann::json& body) const {
  AdminHttpResponse response;
  response.status = status;
  response.content_type = JsonContentType();
  response.headers.push_back({"Cache-Control", "no-store"});
  response.body = body.dump();
  return response;
}

AdminHttpResponse AdminController::HtmlResponse(
    int status, const std::string& body) const {
  AdminHttpResponse response;
  response.status = status;
  response.content_type = "text/html; charset=utf-8";
  response.headers.push_back({"Cache-Control", "no-store"});
  response.body = body;
  return response;
}

AdminHttpResponse AdminController::ErrorResponse(
    int status, const std::string& error) const {
  return JsonResponse(status, {{"ok", false}, {"error", error}});
}
