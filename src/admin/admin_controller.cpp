#include "admin_controller.h"

#include <algorithm>
#include <cctype>
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
    body { margin: 0; background: #f4f6f8; color: #17202a; }
    header { background: #ffffff; border-bottom: 1px solid #d9dee5; padding: 14px 24px; display: flex; align-items: center; justify-content: space-between; }
    h1 { font-size: 20px; margin: 0; }
    main { padding: 24px; max-width: 1280px; margin: 0 auto; }
    button { border: 1px solid #9aa7b5; background: #ffffff; border-radius: 6px; padding: 8px 12px; cursor: pointer; }
    button.primary { background: #1264a3; color: #ffffff; border-color: #1264a3; }
    button.danger { background: #b42318; color: #ffffff; border-color: #b42318; }
    button:disabled { opacity: .6; cursor: not-allowed; }
    input { border: 1px solid #b8c2cc; border-radius: 6px; padding: 9px 10px; }
    .login { min-height: 70vh; display: grid; place-items: center; }
    .panel { background: #ffffff; border: 1px solid #d9dee5; border-radius: 8px; padding: 18px; }
    .login .panel { width: min(360px, calc(100vw - 40px)); display: grid; gap: 12px; }
    .metrics { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 14px; margin-bottom: 18px; }
    .metric { background: #ffffff; border: 1px solid #d9dee5; border-radius: 8px; padding: 16px; }
    .metric span { color: #667085; font-size: 13px; }
    .metric strong { display: block; font-size: 30px; margin-top: 6px; }
    .grid { display: grid; grid-template-columns: 1.1fr .9fr; gap: 18px; align-items: start; }
    .toolbar { display: flex; gap: 10px; justify-content: space-between; align-items: center; margin-bottom: 12px; }
    .toolbar h2 { font-size: 18px; margin: 0; }
    .actions { display: flex; gap: 8px; align-items: center; justify-content: flex-end; flex-wrap: wrap; }
    select { border: 1px solid #b8c2cc; border-radius: 6px; padding: 9px 10px; background: #ffffff; }
    .table-wrap { overflow-x: auto; }
    table { width: 100%; border-collapse: collapse; font-size: 14px; }
    th, td { border-bottom: 1px solid #edf0f3; padding: 10px 8px; text-align: left; vertical-align: top; }
    th { color: #667085; font-weight: 600; }
    .segments { display: flex; gap: 6px; margin: 0 0 12px; overflow-x: auto; padding-bottom: 2px; }
    .segments button { white-space: nowrap; border-color: #d0d5dd; color: #344054; }
    .segments button.active { background: #1264a3; border-color: #1264a3; color: #ffffff; }
    .presence-table th:nth-child(1) { min-width: 190px; }
    .presence-table th:nth-child(3) { min-width: 150px; }
    .presence-table th:nth-child(5) { min-width: 130px; }
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
    .sort-order { min-width: 62px; }
    .pager { display: flex; gap: 8px; align-items: center; justify-content: flex-end; margin-top: 12px; color: #667085; font-size: 13px; flex-wrap: wrap; }
    .status { color: #067647; font-weight: 600; }
    .offline { color: #667085; font-weight: 600; }
    .muted { color: #667085; }
    .error { color: #b42318; min-height: 20px; }
    .empty { color: #667085; text-align: center; padding: 16px 8px; }
    .hidden { display: none; }
    @media (max-width: 860px) { .metrics, .grid { grid-template-columns: 1fr; } header { padding: 12px 16px; } main { padding: 16px; } .toolbar { align-items: stretch; flex-direction: column; } .actions { justify-content: flex-start; } input, select { max-width: 100%; } }
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
              <thead><tr><th>Client</th><th>State</th><th>Seen</th><th>Current online</th><th>Session</th><th>Action</th></tr></thead>
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
          <table>
            <thead><tr><th>Transmission</th><th>Participants</th><th>Action</th></tr></thead>
            <tbody id="sessions"></tbody>
          </table>
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

    function appendBadge(parent, value, className) {
      return appendText(parent, 'span', value, `badge ${className}`);
    }

    function setDurationDataset(cell, kind, device, base, capturedAt) {
      cell.dataset.duration = kind;
      cell.dataset.online = device.online ? '1' : '0';
      cell.dataset.base = String(base || 0);
      cell.dataset.capturedAt = String(capturedAt);
    }

    function sessionSummary(device) {
      const controlling = Number(device.active_control_count) || 0;
      const controlled = Number(device.active_controlled_count) || 0;
      const parts = [];
      if (controlling > 0) parts.push(`controlling ${controlling}`);
      if (controlled > 0) parts.push(`controlled ${controlled}`);
      return parts.join(', ') || '-';
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
        appendEmptyRow(body, 6);
        return;
      }
      const capturedAt = Math.floor(Date.now() / 1000);
      devices.forEach(device => {
        const activeSessions = Number(device.active_session_count) || 0;
        const row = document.createElement('tr');
        const clientCell = document.createElement('td');
        appendText(clientCell, 'div', device.id, 'device-id');
        appendText(clientCell, 'span', device.kind === 'web' ? 'web client' : 'device', 'subline');
        row.appendChild(clientCell);

        const statusCell = document.createElement('td');
        appendBadge(statusCell, device.online ? 'online' : 'offline',
          device.online ? 'online' : 'offline');
        if (activeSessions > 0) appendBadge(statusCell, 'remote', 'active');
        row.appendChild(statusCell);

        const timeCell = document.createElement('td');
        appendText(timeCell, 'div', formatTime(device.online ? device.online_since : device.updated_at));
        appendText(timeCell, 'span', device.online ? 'online since' : 'last online', 'muted');
        row.appendChild(timeCell);

        const currentCell = appendText(row, 'td', device.online ? formatDuration(device.online_duration_seconds) : '-');
        setDurationDataset(currentCell, 'current', device, device.online_duration_seconds, capturedAt);
        appendText(row, 'td', sessionSummary(device));

        const actionCell = document.createElement('td');
        const detailButton = document.createElement('button');
        detailButton.type = 'button';
        detailButton.textContent = expandedDevices.has(device.id) ? 'Hide' : 'Details';
        detailButton.addEventListener('click', () => {
          if (expandedDevices.has(device.id)) expandedDevices.delete(device.id);
          else expandedDevices.add(device.id);
          renderDevices(currentDevices);
        });
        actionCell.appendChild(detailButton);
        row.appendChild(actionCell);
        body.appendChild(row);

        if (expandedDevices.has(device.id)) {
          const detailsRow = document.createElement('tr');
          detailsRow.className = 'details-row';
          const detailsCell = document.createElement('td');
          detailsCell.colSpan = 6;
          const details = document.createElement('div');
          details.className = 'detail-grid';
          const totalOnline = appendDetailItem(
            details, 'Total online', formatDuration(device.total_online_seconds));
          setDurationDataset(totalOnline, 'total', device, device.total_online_seconds, capturedAt);
          appendDetailItem(details, 'Total control', formatDuration(device.total_control_seconds));
          appendDetailItem(details, 'Total controlled', formatDuration(device.total_controlled_seconds));
          appendDetailItem(details, 'Online since', formatTime(device.online_since));
          appendDetailItem(details, 'Last online', formatTime(device.online ? 0 : device.updated_at));
          appendDetailItem(details, 'Active session', sessionSummary(device));
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
        const transmissionCell = document.createElement('td');
        appendText(transmissionCell, 'div', session.transmission_id);
        appendText(transmissionCell, 'span', `host ${session.host_id}`, 'muted');
        row.appendChild(transmissionCell);

        const participantsCell = document.createElement('td');
        appendText(participantsCell, 'div', session.participant_count);
        appendText(participantsCell, 'span', guests, 'muted');
        row.appendChild(participantsCell);

        const actionCell = document.createElement('td');
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
      document.querySelectorAll('[data-duration="current"]').forEach(cell => {
        if (cell.dataset.online !== '1') return;
        const base = Number(cell.dataset.base) || 0;
        const capturedAt = Number(cell.dataset.capturedAt) || now;
        cell.textContent = formatDuration(base + now - capturedAt);
      });
      document.querySelectorAll('[data-duration="total"]').forEach(cell => {
        if (cell.dataset.online !== '1') return;
        const base = Number(cell.dataset.base) || 0;
        const capturedAt = Number(cell.dataset.capturedAt) || now;
        cell.textContent = formatDuration(base + now - capturedAt);
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
                         {"active_control_count", active_control_count},
                         {"active_controlled_count",
                          active_controlled_count},
                         {"active_session_count",
                          active_control_count +
                              active_controlled_count}});
    }
  }

  nlohmann::json sessions = nlohmann::json::array();
  size_t sessions_total = 0;
  if (transmission_) {
    for (const auto& snapshot : transmission_->GetTransmissionSnapshots(
             session_limit, session_offset, session_search, &sessions_total)) {
      sessions.push_back({{"transmission_id", snapshot.transmission_id},
                          {"host_id", snapshot.host_id},
                          {"guest_ids", snapshot.guest_ids},
                          {"participant_count", snapshot.participant_count},
                          {"active", snapshot.active}});
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

  if (!existed) {
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

  return {{"online_device_count",
           presence_ ? presence_->GetOnlineDeviceCount()
                     : online_device_fallback},
          {"online_web_client_count",
           presence_ ? presence_->GetOnlineWebClientCount() : 0},
          {"active_connection_count",
           transmission_ ? transmission_->GetActiveConnectionCount() : 0},
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
