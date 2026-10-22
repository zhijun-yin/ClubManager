// Included by web_server.cpp after its HTTP/JSON helpers. This is the single
// authorization boundary: only an authenticated administrator can fall through.
static std::string headerValue(const std::map<std::string, std::string>& headers, const std::string& key) {
    auto it = headers.find(key); return it == headers.end() ? "" : it->second;
}
static std::string sessionCookie(const std::string& cookies) {
    std::istringstream in(cookies); std::string part, result;
    while (std::getline(in, part, ';')) {
        auto begin = part.find_first_not_of(" \t");
        if (begin == std::string::npos) continue;
        part = part.substr(begin);
        if (part.rfind("club_session=", 0) != 0) continue;
        if (!result.empty()) return "";
        result = part.substr(13);
        if (result.size() != 64 || result.find_first_not_of("0123456789abcdef") != std::string::npos) return "";
    }
    return result;
}
static std::string cookieHeader(const std::string& token) {
    return "Set-Cookie: club_session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
        (token.empty() ? "0" : "28800") + (std::getenv("CLUB_SECURE_COOKIE") ? "; Secure\r\n" : "\r\n");
}
static bool bindingValid(const Account& account, ClubSystem& sys) {
    const auto* member = sys.findMember(account.memberId);
    return member && member->active && !account.memberKey.empty() &&
        club_crypto::constantEqual(member->accountKey, account.memberKey);
}
static std::string publicAccount(const Account& a, ClubSystem& sys) {
    return "{\"username\":\"" + jsonEscape(a.username) + "\",\"role\":\"" + (a.admin ? "admin" : "member") +
        "\",\"memberId\":" + std::to_string(a.memberId) + ",\"enabled\":" + (a.enabled ? "true" : "false") +
        ",\"bindingValid\":" + (bindingValid(a, sys) ? "true" : "false") + "}";
}
static bool authorizeAndRespond(sock_t s, const std::string& method, const std::vector<std::string>& seg,
    Form& f, ClubSystem& sys, AuthStore& auth, const std::map<std::string, std::string>& headers,
    const std::string& client) {
    auto reply = [&](int code, const std::string& value, const std::string& extra = "") {
        httpResponse(s, code, code == 200 ? "OK" : code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" :
            code == 429 ? "Too Many Requests" : "Bad Request", "application/json", value, extra);
    };
    auto deny = [&]() { reply(403, jErr("没有权限执行此操作；普通成员只能管理自己的报名、签到和入社申请")); return true; };
    for (const auto& item : f) if (item.second.size() != 1) {
        reply(400, jErr("请求包含重复参数")); return true;
    }
    const auto token = sessionCookie(headerValue(headers, "cookie"));
    const auto* session = auth.session(token);
    const Account* current = session ? auth.account(session->username) : nullptr;
    if (current && !current->admin && !bindingValid(*current, sys)) {
        auth.logout(token); session = nullptr; current = nullptr;
    }
    const bool authRoute = seg.size() == 3 && seg[0] == "api" && seg[1] == "auth";
    if (authRoute && seg[2] == "status" && method == "GET") {
        std::string state = std::string("{\"initialized\":") + (auth.initialized() ? "true" : "false") +
            ",\"authenticated\":" + (current ? "true" : "false");
        if (current) state += ",\"user\":" + publicAccount(*current, sys) + ",\"csrf\":\"" + session->csrf + "\"";
        reply(200, jOk(state + "}")); return true;
    }
    if (authRoute && seg[2] == "login" && method == "POST") {
        if (!auth.initialized()) { reply(403, jErr("请在服务器本机初始化管理员后重启服务")); return true; }
        if (auth.loginBlocked(client)) { reply(429, jErr("尝试过于频繁，请在一分钟后重试"), "Retry-After: 60\r\n"); return true; }
        const auto username = formGet(f, "username");
        bool valid = auth.verify(username, formGet(f, "password"));
        const auto* a = auth.account(username);
        if (!valid || (!a->admin && !bindingValid(*a, sys))) {
            auth.loginFailed(client); reply(401, jErr("账号或密码错误，或账号已停用 / 未有效绑定成员")); return true;
        }
        // Copy before invalidating any old session.
        auto user = publicAccount(*a, sys);
        const auto newToken = auth.newSession(username);
        auth.loginSucceeded(client);
        reply(200, jOk("{\"user\":" + user + ",\"csrf\":\"" + auth.session(newToken)->csrf + "\"}"), cookieHeader(newToken));
        return true;
    }
    if (!current) { reply(401, jErr("请先登录，或登录已失效"), cookieHeader("")); return true; }
    // Copy principal: update/reset/logout below may invalidate its storage.
    const Account principal = *current;
    if (method != "GET" && !club_crypto::constantEqual(headerValue(headers, "x-csrf-token"), session->csrf)) return deny();
    if (authRoute && seg[2] == "logout" && method == "POST") {
        auth.logout(token); reply(200, jOk("{}"), cookieHeader("")); return true;
    }
    if (authRoute && seg[2] == "password" && method == "POST") {
        if (auth.loginBlocked(client)) { reply(429, jErr("尝试过于频繁，请稍后重试")); return true; }
        if (!auth.verify(principal.username, formGet(f, "currentPassword"))) {
            auth.loginFailed(client); reply(400, jErr("当前密码错误")); return true;
        }
        const auto error = auth.resetPassword(principal.username, formGet(f, "password"));
        if (error.empty()) auth.loginSucceeded(client);
        reply(error.empty() ? 200 : 400, error.empty() ? jOk("{}") : jErr(error), error.empty() ? cookieHeader("") : "");
        return true;
    }
    if (seg.size() >= 2 && seg[0] == "api" && seg[1] == "accounts") {
        if (!principal.admin) return deny();
        if (method == "GET" && seg.size() == 2) {
            std::string data = "{\"accounts\":["; bool first = true;
            for (const auto& a : auth.accounts()) { if (!first) data += ','; first = false; data += publicAccount(a, sys); }
            reply(200, jOk(data + "]}")); return true;
        }
        std::string error;
        if (method == "POST" && seg.size() == 4 && seg[3] == "password") {
            error = auth.resetPassword(seg[2], formGet(f, "password"));
        } else if (method == "POST" && (seg.size() == 2 || seg.size() == 3)) {
            const auto role = formGet(f, "role");
            if (role != "admin" && role != "member") { reply(400, jErr("角色必须为 admin 或 member")); return true; }
            const auto enabled = formGet(f, "enabled", "true");
            if (enabled != "true" && enabled != "false") { reply(400, jErr("启用状态无效")); return true; }
            int memberId = toInt(formGet(f, "memberId", "-1"));
            std::string memberKey;
            if (memberId >= 0) {
                auto* member = sys.findMember(memberId);
                if (!member || !member->active) { reply(400, jErr("请绑定在册成员")); return true; }
                if (member->accountKey.empty()) {
                    member->accountKey = club_crypto::randomHex(32);
                    if (!sys.save(SAVE_FILE)) { member->accountKey.clear(); reply(400, jErr("成员身份保存失败")); return true; }
                }
                memberKey = member->accountKey;
            }
            if (seg.size() == 2) error = auth.create(formGet(f,"username"), formGet(f,"password"), role == "admin", memberId, memberKey);
            else error = auth.update(seg[2], role == "admin", enabled == "true", memberId, memberKey);
        } else { reply(400, jErr("未知账号操作")); return true; }
        reply(error.empty() ? 200 : 400, error.empty() ? jOk("{}") : jErr(error)); return true;
    }
    if (principal.admin) return false;

    // Member allowlist. Identity always comes from the authenticated binding.
    const int id = principal.memberId;
    if (seg.size() < 2 || seg[0] != "api") return deny();
    if (method == "GET") {
        std::string data;
        if (seg.size() == 2 && seg[1] == "members") {
            data = "{\"members\":[" + memberJson(*sys.findMember(id)) + "]}";
        } else if (seg.size() == 2 && seg[1] == "clubs") {
            data = "{\"clubs\":["; bool first = true;
            for (const auto& club : sys.clubs()) if (club.active) { if (!first) data += ','; first = false; data += clubJson(club,id); }
            data += "]}";
        } else if ((seg.size() == 2 && seg[1] == "activities") ||
                   (seg.size() == 3 && seg[1] == "schedule" && seg[2] == std::to_string(id))) {
            data = "{\"activities\":["; bool first = true;
            std::vector<int> ids;
            if (seg[1] == "schedule") ids = sys.memberSchedule(id);
            else for (const auto& a : sys.activities()) if (a.active) ids.push_back(a.id);
            for (int aid : ids) { if (!first) data += ','; first = false; data += activityJson(*sys.findActivity(aid),id); }
            data += "]}";
        } else if (seg.size() == 2 && seg[1] == "applications") {
            data = "{\"applications\":["; bool first = true;
            for (const auto& a : sys.applications()) if (a.memberId == id) { if (!first) data += ','; first = false; data += appJson(a); }
            data += "]}";
        } else if (seg.size() == 3 && seg[1] == "applications" && seg[2] == "pending") {
            int count = 0; for (const auto& a : sys.applications()) if (a.memberId == id && a.status == 0) ++count;
            data = "{\"count\":" + std::to_string(count) + "}";
        } else if (seg.size() == 4 && seg[1] == "announcements" && seg[2] == "member" && seg[3] == std::to_string(id)) {
            data = "{\"announcements\":["; bool first = true;
            for (const auto& a : sys.announcementsForMember(id)) { if (!first) data += ','; first = false; data += annJson(a); }
            data += "]}";
        } else return deny();
        reply(200, jOk(data)); return true;
    }
    if (method != "POST") return deny();
    if (f.count("memberId") && formGet(f,"memberId") != std::to_string(id)) return deny();
    bool ok = false; std::string result = "{}", error = "操作失败，请刷新后检查当前状态";
    if (seg.size() == 4 && seg[1] == "activities") {
        const int aid = toInt(seg[2]);
        if (seg[3] == "signup") {
            const auto message = sys.signup(id, aid);
            ok = message.rfind("成功", 0) == 0;
            result = "{\"message\":\"" + jsonEscape(message) + "\"}"; error = message;
        } else if (seg[3] == "cancel") ok = sys.cancelSignup(id, aid);
        else if (seg[3] == "checkin") ok = sys.checkIn(id, aid);
        else return deny();
    } else if (seg.size() == 2 && seg[1] == "applications") {
        if (f.count("priority") && formGet(f,"priority") != "0") return deny();
        const int aid = sys.submitApplication(id, toInt(formGet(f,"clubId")), 0);
        ok = aid >= 0; result = "{\"id\":" + std::to_string(aid) + "}";
    } else if (seg.size() == 4 && seg[1] == "applications" && seg[3] == "withdraw") {
        int aid = toInt(seg[2]);
        auto it = std::find_if(sys.applications().begin(), sys.applications().end(), [=](const Application& a){return a.id == aid && a.memberId == id;});
        if (it == sys.applications().end()) return deny();
        ok = sys.withdrawApplication(aid, id);
    } else return deny();
    if (ok && !sys.save(SAVE_FILE)) { ok = false; error = "操作已生效，但存档失败，请联系管理员保存"; }
    reply(200, ok ? jOk(result) : jErr(error)); return true;
}
