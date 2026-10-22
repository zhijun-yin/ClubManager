#ifndef CLUB_AUTH_H
#define CLUB_AUTH_H
#include <chrono>
#include <map>
#include <string>
#include <vector>

// 使用操作系统/OpenSSL 的密码学原语，不自行实现密码算法。
namespace club_crypto {
std::string randomHex(size_t bytes);
std::string passwordHash(const std::string& password, const std::string& saltHex);
bool constantEqual(const std::string& a, const std::string& b);
}

struct Account {
    std::string username;
    bool admin = false;
    int memberId = -1;
    std::string memberKey; // 持久化身份，防止导入另一份数据后 ID 重用造成串号
    bool enabled = true;
    std::string salt, hash;
};

class AuthStore {
public:
    explicit AuthStore(std::string path = "club_accounts.txt");
    bool initialized() const { return !accounts_.empty(); }
    const std::vector<Account>& accounts() const { return accounts_; }
    const Account* account(const std::string& username) const;
    std::string initialize(const std::string& username, const std::string& password);
    std::string create(const std::string& username, const std::string& password, bool admin,
                       int memberId, const std::string& memberKey);
    std::string update(const std::string& username, bool admin, bool enabled,
                       int memberId, const std::string& memberKey);
    std::string resetPassword(const std::string& username, const std::string& password);
    bool verify(const std::string& username, const std::string& password) const;
    struct Session {
        std::string username, csrf;
        std::chrono::steady_clock::time_point expires;
    };
    std::string newSession(const std::string& username);
    const Session* session(const std::string& token);
    void logout(const std::string& token);
    void invalidate(const std::string& username);
    bool loginBlocked(const std::string& client);
    void loginFailed(const std::string& client);
    void loginSucceeded(const std::string& client);
private:
    std::string path_;
    std::vector<Account> accounts_;
    std::map<std::string, Session> sessions_;
    struct Attempts { int failures = 0; std::chrono::steady_clock::time_point expires; };
    std::map<std::string, Attempts> attempts_;
    bool persist() const;
    std::string validateBinding(const std::string& username, bool admin, int memberId,
                                const std::string& memberKey) const;
};

// 本机维护入口：密码从隐藏输入/标准输入读取，不放入命令行参数。
std::string readPassword(const std::string& prompt);
#endif
