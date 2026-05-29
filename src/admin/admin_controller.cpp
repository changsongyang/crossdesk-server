#include "admin_controller.h"

#include <algorithm>

namespace {

constexpr char kSessionCookieName[] = "cd_admin_session";
constexpr char kDisconnectPrefix[] = "/api/admin/sessions/";
constexpr char kDisconnectSuffix[] = "/disconnect";

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
    .metrics { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 14px; margin-bottom: 18px; }
    .metric { background: #ffffff; border: 1px solid #d9dee5; border-radius: 8px; padding: 16px; }
    .metric span { color: #667085; font-size: 13px; }
    .metric strong { display: block; font-size: 30px; margin-top: 6px; }
    .grid { display: grid; grid-template-columns: 1.1fr .9fr; gap: 18px; align-items: start; }
    .toolbar { display: flex; gap: 10px; justify-content: space-between; align-items: center; margin-bottom: 12px; }
    table { width: 100%; border-collapse: collapse; font-size: 14px; }
    th, td { border-bottom: 1px solid #edf0f3; padding: 10px 8px; text-align: left; vertical-align: top; }
    th { color: #667085; font-weight: 600; }
    .status { color: #067647; font-weight: 600; }
    .muted { color: #667085; }
    .error { color: #b42318; min-height: 20px; }
    .hidden { display: none; }
    @media (max-width: 860px) { .metrics, .grid { grid-template-columns: 1fr; } header { padding: 12px 16px; } main { padding: 16px; } }
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
        <div id="refresh-error" class="error"></div>
      </div>
      <div class="metrics">
        <div class="metric"><span>Online devices</span><strong id="metric-devices">0</strong></div>
        <div class="metric"><span>Web clients</span><strong id="metric-web">0</strong></div>
        <div class="metric"><span>Active sessions</span><strong id="metric-sessions">0</strong></div>
      </div>
      <div class="grid">
        <section class="panel">
          <div class="toolbar">
            <h2>Online Devices</h2>
            <input id="device-search" placeholder="Search device ID">
          </div>
          <table>
            <thead><tr><th>Device ID</th><th>Status</th><th>Updated</th></tr></thead>
            <tbody id="devices"></tbody>
          </table>
        </section>
        <section class="panel">
          <h2>Active Sessions</h2>
          <table>
            <thead><tr><th>Transmission</th><th>Participants</th><th>Action</th></tr></thead>
            <tbody id="sessions"></tbody>
          </table>
        </section>
      </div>
    </section>
  </main>
  <script>
    const loginView = document.getElementById('login-view');
    const dashboardView = document.getElementById('dashboard-view');
    const logoutButton = document.getElementById('logout');
    let devices = [];
    let timer = null;

    function showDashboard() {
      loginView.classList.add('hidden');
      dashboardView.classList.remove('hidden');
      logoutButton.classList.remove('hidden');
      refresh();
      if (!timer) timer = setInterval(refresh, 5000);
    }

    function showLogin(message) {
      dashboardView.classList.add('hidden');
      loginView.classList.remove('hidden');
      logoutButton.classList.add('hidden');
      if (timer) clearInterval(timer);
      timer = null;
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

    function renderDevices() {
      const query = document.getElementById('device-search').value.trim();
      const body = document.getElementById('devices');
      body.innerHTML = '';
      devices.filter(d => !query || d.id.includes(query)).forEach(device => {
        const row = document.createElement('tr');
        row.innerHTML = `<td>${device.id}</td><td class="status">online</td><td>${formatTime(device.updated_at)}</td>`;
        body.appendChild(row);
      });
    }

    function renderSessions(sessions) {
      const body = document.getElementById('sessions');
      body.innerHTML = '';
      sessions.forEach(session => {
        const guests = session.guest_ids.join(', ') || '-';
        const row = document.createElement('tr');
        row.innerHTML = `<td>${session.transmission_id}<br><span class="muted">host ${session.host_id}</span></td><td>${session.participant_count}<br><span class="muted">${guests}</span></td><td><button class="danger" data-id="${session.transmission_id}" data-host="${session.host_id}">Disconnect</button></td>`;
        body.appendChild(row);
      });
      body.querySelectorAll('button').forEach(button => {
        button.addEventListener('click', () => disconnectSession(button.dataset.id, button.dataset.host, button));
      });
    }

    async function refresh() {
      const response = await fetch('/api/admin/overview', {credentials: 'same-origin'});
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
      document.getElementById('metric-devices').textContent = data.stats.online_device_count;
      document.getElementById('metric-web').textContent = data.stats.online_web_client_count;
      document.getElementById('metric-sessions').textContent = data.stats.active_connection_count;
      document.getElementById('last-refresh').textContent = new Date().toLocaleTimeString();
      devices = data.devices || [];
      renderDevices();
      renderSessions(data.sessions || []);
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
      if (response.ok) refresh();
      else document.getElementById('refresh-error').textContent = 'Failed to disconnect session';
    }

    document.getElementById('login-form').addEventListener('submit', login);
    document.getElementById('logout').addEventListener('click', logout);
    document.getElementById('device-search').addEventListener('input', renderDevices);
    refresh().then((ok) => {
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
  return resource == "/admin" || resource == "/api/admin" ||
         resource.rfind("/api/admin/", 0) == 0;
}

std::string AdminController::ExtractDisconnectTransmissionId(
    const std::string& resource) {
  if (resource.rfind(kDisconnectPrefix, 0) != 0) {
    return "";
  }

  std::string tail = resource.substr(std::string(kDisconnectPrefix).size());
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
  if (request.resource == "/admin") {
    return HandleAdminPage();
  }

  if (!auth_ || !auth_->IsEnabled()) {
    return ErrorResponse(503, "admin_disabled");
  }

  if (request.resource == "/api/admin/login") {
    return HandleLogin(request);
  }

  if (!IsAuthorized(request)) {
    return ErrorResponse(401, "unauthorized");
  }

  if (request.resource == "/api/admin/logout") {
    return HandleLogout(request);
  }
  if (request.resource == "/api/admin/overview") {
    return HandleOverview(request);
  }
  if (!ExtractDisconnectTransmissionId(request.resource).empty()) {
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

AdminHttpResponse AdminController::HandleOverview(
    const AdminHttpRequest& request) {
  if (request.method != "GET") {
    return ErrorResponse(405, "method_not_allowed");
  }

  nlohmann::json devices = nlohmann::json::array();
  if (db_) {
    for (const auto& device : db_->ListOnlineDevices()) {
      devices.push_back({{"id", device.device_id},
                         {"online", device.online},
                         {"kind", "device"},
                         {"updated_at", device.updated_at}});
    }
  }

  nlohmann::json sessions = nlohmann::json::array();
  if (transmission_) {
    for (const auto& snapshot : transmission_->GetTransmissionSnapshots()) {
      sessions.push_back({{"transmission_id", snapshot.transmission_id},
                          {"host_id", snapshot.host_id},
                          {"guest_ids", snapshot.guest_ids},
                          {"participant_count", snapshot.participant_count},
                          {"active", snapshot.active}});
    }
  }

  nlohmann::json stats = {
      {"online_device_count",
       presence_ ? presence_->GetOnlineDeviceCount() : devices.size()},
      {"online_web_client_count",
       presence_ ? presence_->GetOnlineWebClientCount() : 0},
      {"active_connection_count",
       transmission_ ? transmission_->GetActiveConnectionCount() : 0}};

  return JsonResponse(200,
                      {{"stats", stats}, {"devices", devices}, {"sessions", sessions}});
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
