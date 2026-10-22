#include "auth.h"
#include "data_io.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
constexpr int ITERATIONS = 600000;
using Clock = std::chrono::steady_clock;
bool hex(const std::string& value, size_t length) {
    return value.size() == length && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string toHex(const unsigned char* data, size_t size) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < size; ++i) { result += digits[data[i] >> 4]; result += digits[data[i] & 15]; }
    return result;
}
bool validUsername(const std::string& name) {
    return name.size() >= 3 && name.size() <= 32 &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == std::string::npos;
}
bool validPassword(const std::string& password) { return password.size() >= 10 && password.size() <= 128; }
bool accountLine(std::istream& input, std::string& line) {
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}
}

std::string club_crypto::randomHex(size_t bytes) {
    if (bytes == 0 || bytes > 64) throw std::invalid_argument("random size");
    std::vector<unsigned char> data(bytes);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, data.data(), static_cast<ULONG>(data.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("secure random failed");
#else
    if (RAND_bytes(data.data(), static_cast<int>(data.size())) != 1) throw std::runtime_error("secure random failed");
#endif
    return toHex(data.data(), data.size());
}

std::string club_crypto::passwordHash(const std::string& password, const std::string& saltHex) {
    if (!hex(saltHex, 32) || password.size() > 128) throw std::invalid_argument("invalid password parameters");
    std::array<unsigned char, 16> salt{};
    for (size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<unsigned char>(std::stoul(saltHex.substr(i*2, 2), nullptr, 16));
    std::array<unsigned char, 32> output{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
        throw std::runtime_error("password provider failed");
    auto status = BCryptDeriveKeyPBKDF2(algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
        salt.data(), static_cast<ULONG>(salt.size()), ITERATIONS, output.data(), static_cast<ULONG>(output.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("password hash failed");
#else
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()),
        ITERATIONS, EVP_sha256(), static_cast<int>(output.size()), output.data()) != 1) throw std::runtime_error("password hash failed");
#endif
    return toHex(output.data(), output.size());
}

bool club_crypto::constantEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i) difference = static_cast<unsigned char>(difference | (a[i] ^ b[i]));
    return difference == 0;
}

AuthStore::AuthStore(std::string path) : path_(std::move(path)) {
    if (!std::filesystem::exists(std::filesystem::u8path(path_))) return;
    std::ifstream in(std::filesystem::u8path(path_));
    std::string line;
    if (!accountLine(in, line) || line != "# ClubManager accounts v1 PBKDF2-SHA256 600000")
        throw std::runtime_error("账号文件损坏，已停止启动，不会重置现有账号");
    bool end = false;
    int admins = 0;
    while (accountLine(in, line)) {
        if (line == "END") { end = true; break; }
        if (accounts_.size() >= 10000) throw std::runtime_error("too many accounts");
        auto fields = std::vector<std::string>{};
        std::istringstream stream(line); std::string field;
        while (std::getline(stream, field, '|')) fields.push_back(field);
        if (fields.size() != 7) throw std::runtime_error("账号文件格式错误");
        Account a;
        a.username = fields[0];
        if (!validUsername(a.username) || account(a.username) || (fields[1] != "admin" && fields[1] != "member")) throw std::runtime_error("账号信息无效");
        a.admin = fields[1] == "admin";
        size_t used = 0; a.memberId = std::stoi(fields[2], &used);
        if (used != fields[2].size() || a.memberId < -1) throw std::runtime_error("账号绑定无效");
        a.memberKey = fields[3] == "-" ? "" : fields[3];
        if ((fields[4] != "0" && fields[4] != "1") || !hex(fields[5],32) || !hex(fields[6],64)) throw std::runtime_error("账号凭据无效");
        a.enabled = fields[4] == "1"; a.salt = fields[5]; a.hash = fields[6];
        if (!validateBinding(a.username, a.admin, a.memberId, a.memberKey).empty()) throw std::runtime_error("账号绑定重复或无效");
        if (a.admin && a.enabled) ++admins;
        accounts_.push_back(a);
    }
    if (!end || admins == 0 || in.bad()) throw std::runtime_error("账号文件不完整或没有可用管理员");
    while (accountLine(in,line)) if (!line.empty()) throw std::runtime_error("账号文件有多余内容");
}

const Account* AuthStore::account(const std::string& username) const {
    for (const auto& a : accounts_) if (a.username == username) return &a;
    return nullptr;
}
bool AuthStore::persist() const {
    std::ostringstream out;
    out << "# ClubManager accounts v1 PBKDF2-SHA256 600000\n";
    for (const auto& a : accounts_) out << a.username << '|' << (a.admin ? "admin" : "member") << '|'
        << a.memberId << '|' << (a.memberKey.empty() ? "-" : a.memberKey) << '|' << a.enabled << '|' << a.salt << '|' << a.hash << '\n';
    out << "END\n";
    return club_io::atomicWrite(path_, out.str());
}
std::string AuthStore::validateBinding(const std::string& username, bool admin, int memberId, const std::string& memberKey) const {
    if (memberId < -1 || (memberId == -1 && (!admin || !memberKey.empty())) || (memberId >= 0 && !hex(memberKey,64)))
        return "普通成员账号必须绑定有效成员";
    for (const auto& a : accounts_) if (a.username != username && memberId >= 0 && a.memberId == memberId && a.memberKey == memberKey)
        return "该成员已绑定其他账号";
    return "";
}
std::string AuthStore::initialize(const std::string& username, const std::string& password) {
    if (initialized()) return "已有管理员，不能重复初始化";
    return create(username, password, true, -1, "");
}
std::string AuthStore::create(const std::string& username, const std::string& password, bool admin, int memberId, const std::string& memberKey) {
    if (accounts_.size() >= 10000) return "账号数量已达上限";
    if (!validUsername(username)) return "账号须为 3–32 位英文字母、数字、点、下划线或横线";
    if (!validPassword(password)) return "密码须为 10–128 字节";
    if (account(username)) return "账号已存在";
    auto error = validateBinding(username, admin, memberId, memberKey); if (!error.empty()) return error;
    if (!initialized() && !admin) return "请先初始化管理员";
    Account a{username, admin, memberId, memberKey, true, club_crypto::randomHex(16), ""};
    a.hash = club_crypto::passwordHash(password, a.salt);
    accounts_.push_back(a);
    if (!persist()) { accounts_.pop_back(); return "账号保存失败"; }
    return "";
}
std::string AuthStore::update(const std::string& username, bool admin, bool enabled, int memberId, const std::string& memberKey) {
    auto error = validateBinding(username, admin, memberId, memberKey); if (!error.empty()) return error;
    int admins = 0;
    for (const auto& a : accounts_) if (a.admin && a.enabled) ++admins;
    for (auto& a : accounts_) if (a.username == username) {
        if (a.admin && a.enabled && (!admin || !enabled) && admins <= 1) return "不能停用或降级最后一个管理员";
        Account before = a;
        a.admin = admin; a.enabled = enabled; a.memberId = memberId; a.memberKey = memberKey;
        if (!persist()) { a = before; return "账号保存失败"; }
        invalidate(username); return "";
    }
    return "账号不存在";
}
std::string AuthStore::resetPassword(const std::string& username, const std::string& password) {
    if (!validPassword(password)) return "密码须为 10–128 字节";
    for (auto& a : accounts_) if (a.username == username) {
        Account before = a;
        a.salt = club_crypto::randomHex(16); a.hash = club_crypto::passwordHash(password, a.salt);
        if (!persist()) { a = before; return "账号保存失败"; }
        invalidate(username); return "";
    }
    return "账号不存在";
}
bool AuthStore::verify(const std::string& username, const std::string& password) const {
    if (password.size() > 128) return false;
    const auto* a = account(username);
    // 不存在账号也执行 KDF，避免用响应时间探测账号。
    auto computed = club_crypto::passwordHash(password, a ? a->salt : std::string(32,'0'));
    return a && a->enabled && club_crypto::constantEqual(computed, a->hash);
}
std::string AuthStore::newSession(const std::string& username) {
    const auto* a = account(username);
    if (!a || !a->enabled) throw std::invalid_argument("invalid session account");
    auto now = Clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.expires <= now) it = sessions_.erase(it); else ++it;
    }
    // 每个账号只保留当前登录，会使旧浏览器会话立即失效。
    invalidate(username);
    if (sessions_.size() >= 2048) throw std::runtime_error("会话数量已达上限");
    auto token = club_crypto::randomHex(32);
    sessions_[token] = {username, club_crypto::randomHex(32), now + std::chrono::hours(8)};
    return token;
}
const AuthStore::Session* AuthStore::session(const std::string& token) {
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return nullptr;
    const auto* a = account(it->second.username);
    if (it->second.expires <= Clock::now() || !a || !a->enabled) { sessions_.erase(it); return nullptr; }
    return &it->second;
}
void AuthStore::logout(const std::string& token) { sessions_.erase(token); }
void AuthStore::invalidate(const std::string& username) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.username == username) it = sessions_.erase(it); else ++it;
    }
}
bool AuthStore::loginBlocked(const std::string& client) {
    auto now = Clock::now();
    for (auto it = attempts_.begin(); it != attempts_.end();) {
        if (it->second.expires <= now) it = attempts_.erase(it); else ++it;
    }
    auto it = attempts_.find(client);
    return it == attempts_.end() ? attempts_.size() >= 1024 : it->second.failures >= 5;
}
void AuthStore::loginFailed(const std::string& client) {
    auto& value = attempts_[client];
    if (!value.failures) value.expires = Clock::now() + std::chrono::seconds(60);
    ++value.failures;
}
void AuthStore::loginSucceeded(const std::string& client) { attempts_.erase(client); }

std::string readPassword(const std::string& prompt) {
    std::cerr << prompt;
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE); DWORD mode = 0;
    bool console = GetConsoleMode(input, &mode) != 0;
    if (console) SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT);
#else
    termios old{}; bool console = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (console) { auto quiet = old; quiet.c_lflag &= ~ECHO; tcsetattr(STDIN_FILENO,TCSAFLUSH,&quiet); }
#endif
    std::string password; std::getline(std::cin, password);
#ifdef _WIN32
    if (console) SetConsoleMode(input, mode);
#else
    if (console) tcsetattr(STDIN_FILENO,TCSAFLUSH,&old);
#endif
    if (console) std::cerr << '\n';
    if (!std::cin) throw std::runtime_error("无法读取密码");
    return password;
}
