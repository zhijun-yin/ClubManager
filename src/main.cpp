#include "system.h"
#include "web_server.h"
#include "data_io.h"
#include "auth.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
// Windows 控制台默认 GBK，这里把 argv 转为 UTF-8 以正确处理中文
static std::vector<std::string> utf8Args(int& argc) {
    wchar_t** wargv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (wargv) {
        for (int i = 0; i < argc; ++i) {
            int len = ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0,
                                            nullptr, nullptr);
            std::string s(len > 0 ? len : 0, '\0');
            if (len > 1) {
                ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len,
                                      nullptr, nullptr);
            }
            if (!s.empty()) s.pop_back();
            args.push_back(s);
        }
        ::LocalFree(wargv);
    }
    return args;
}
#endif

static int parseInt(const std::string& value) {
    size_t used = 0;
    int result = std::stoi(value, &used);
    if (used != value.size()) throw std::invalid_argument("整数参数格式错误");
    return result;
}
static size_t parseCount(const std::string& value) {
    int result = parseInt(value);
    if (result < 0) throw std::invalid_argument("数量不能为负数");
    return static_cast<size_t>(result);
}

static void usage() {
    std::cout <<
        "社团管理系统 ClubManager —— 命令行\n"
        "用法: club <命令> [参数...]\n\n"
        "  成员:\n"
        "    member add <姓名> <年级> [tag:标签...]   注册成员（标签用 tag: 前缀）\n"
        "    member remove <id>                       删除成员（有报名/待审则拒绝）\n"
        "    member score <id> <增减>                 调整活跃度\n"
        "    member tag <id> <标签>                   给成员打标签\n"
        "    member untag <id> <标签>                 删除成员标签\n"
        "    member tags <id>                         查看成员的标签\n"
        "    member bytag <标签>                      查含某标签的成员(倒排索引)\n"
        "    member bytags all|any <t1> <t2>...       多标签查询(AND/OR)\n"
        "    member alltags                           列出所有标签\n"
        "    member import <文件>                      从 CSV 批量导入(姓名,年级,标签1;标签2)\n"
        "    member list                              列出成员\n"
        "    member find <姓名>                       按姓名查 id\n"
        "  社团:\n"
        "    club create <名称> <分类> <成立> <简介>\n"
        "    club sub <父id> <名称> <分类> <成立> <简介>   创建子部门（层级树）\n"
        "    club org                                  打印组织架构(DFS 前序)\n"
        "    club remove <id>                          删除社团（有在册成员/子部门则拒绝）\n"
        "    club join <clubId> <memberId>             入社\n"
        "    club leave <clubId> <memberId>            退社\n"
        "    club list                                 列出社团\n"
        "  活动:\n"
        "    activity add <clubId> <名称> <时间> <容量>\n"
        "    activity addtimed <clubId> <名称> <容量> <日> <起> <止> [tag:标签...]\n"
        "    activity remove <id>\n"
        "    activity signup <memberId> <actId>        报名（满员进候补；时间冲突则拒绝）\n"
        "    activity cancel <memberId> <actId>        取消报名（自动提拔候补）\n"
        "    activity cap <actId> <新容量>             调整容量（自动补位/降级）\n"
        "    activity list\n"
        "    activity waitlist <actId>                 查看候补名单\n"
        "  申请审批(优先级队列):\n"
        "    app submit <memberId> <clubId> [优先级]   提交申请（优先级高者优先，同优先先提交）\n"
        "    app process                               处理优先级最高的待审\n"
        "    app review <appId> <approve|reject>       审批\n"
        "    app pending                                待审数量\n"
        "  好友(图):\n"
        "    friend add <a> <b> / remove <a> <b>\n"
        "    friend recommend <id>                      二度人脉推荐\n"
        "    friend distance <a> <b>                    最短关系距离(BFS)\n"
        "    friend path <a> <b>                        最短社交路径(含端点)\n"
        "    friend recact <id>                         好友驱动的活动推荐\n"
        "  排行/统计(AVL):\n"
        "    rank leaderboard [k]                       Top-K 活跃度\n"
        "    rank member <id>                           我的排名\n"
        "    rank range <lo> <hi>                       区间活跃人数\n"
        "    rank clubleader [k]                        社团活跃度排行\n"
        "  公告广播:\n"
        "    announce broadcast <clubId> <senderId> <消息...>\n"
        "    announce club <clubId>                      某社团的公告\n"
        "    announce mine <memberId>                    我能收到的公告\n"
        "  其他:\n"
        "    auth init <username>                       本机初始化管理员（隐藏输入密码）\n"
        "    auth reset <username>                      停服后在本机重置密码\n"
        "    stats                                      统计总览\n"
        "    schedule <memberId>                        个人活动日程\n"
        "    activity checkin <memberId> <actId>         签到并奖励 10 分\n"
        "    activity close <actId>                     结束活动，保留历史\n"
        "    app withdraw <appId> <memberId>             撤回本人申请\n"
        "    export members <path.csv>                  导出成员\n"
        "    export roster <actId> <path.csv>           导出活动名单\n"
        "    gui                                         启动局域网多人共用 Web 图形界面(浏览器打开)\n"
        "    undo / redo                                撤销 / 重做\n"
        "    save <path> / load <path>                   持久化\n"
        "    demo                                        运行内置演示\n";
}

static ClubSystem sys;

// ------- 辅助打印 -------
static void printMembers() {
    for (const auto& m : sys.members()) {
        if (!m.active) continue;
        std::cout << "  #" << m.id << " " << m.name << " (" << m.grade
                  << ") 活跃度=" << m.score << " 排名=" << sys.memberRank(m.id);
        if (!m.tags.empty()) {
            std::cout << " 标签=[";
            for (size_t i = 0; i < m.tags.size(); ++i)
                std::cout << (i ? ";" : "") << m.tags[i];
            std::cout << "]";
        }
        std::cout << "\n";
    }
}

// ------- 演示 -------
static int runDemo() {
    std::cout << "=== 社团管理系统演示 ===\n\n";

    int alice = sys.addMember("Alice", "2022", {"Python", "算法"});
    int bob = sys.addMember("Bob", "2022", {"C++", "算法"});
    int carol = sys.addMember("Carol", "2021", {"Python"});
    int dave = sys.addMember("Dave", "2023", {"Java"});
    std::cout << "[1] 注册 4 名成员(带标签): Alice#" << alice << " Bob#" << bob
              << " Carol#" << carol << " Dave#" << dave << "\n";

    std::cout << "    倒排索引「Python」持有者:";
    for (int id : sys.membersWithTag("Python")) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?")<<"(#"<<id<<")"; }
    std::cout << "\n";
    std::cout << "    同时会 Python 与算法 的人(AND):";
    for (int id : sys.membersWithAllTags({"Python", "算法"})) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?"); }
    std::cout << "\n";

    sys.addScore(carol, 30);
    sys.addScore(alice, 20);
    sys.addScore(bob, 10);
    std::cout << "[2] 调整活跃度后排行榜(Top3):\n";
    for (auto& [score, id] : sys.leaderboard(3)) {
        const Member* m = sys.findMember(id);
        std::cout << "      " << (m ? m->name : "?") << " 活跃度=" << score << "\n";
    }

    int coding = sys.createClub("编程社", "学术科技", "2020-09", "热爱编程");
    sys.joinClub(coding, alice);
    sys.joinClub(coding, carol);
    int algoDept = sys.createSubClub(coding, "算法组", "学术科技", "2021-03", "专攻算法");
    sys.joinClub(algoDept, bob);
    std::cout << "[3] 创建「编程社」#" << coding <<"，子部门「算法组」#" << algoDept
              << "，Alice、Carol 入社，Bob 进算法组\n";
    std::cout << "    组织架构:\n";
    for (auto& line : sys.orgTreeLines()) std::cout << "      " << line << "\n";

    int act = sys.addActivity(coding, "算法周赛", "周六 19:00", 2);
    int lateNight = sys.addActivity(coding, "深夜编程局", "", 5, {"Python"}, 5, 22 * 60, 23 * 60);
    int nightTalk = sys.addActivity(coding, "夜谈会", "", 5, {}, 5, 22 * 60 + 45, 23 * 60 + 15);
    std::cout << "[4] 创建「算法周赛」#" << act << "（容量=2），及两个结构化时间段活动：\n"
              << "    「深夜编程局」#" << lateNight << "（第5天 22:00-23:00）、「夜谈会」#" << nightTalk
              << "（第5天 22:45-23:15，与前者时间重叠）\n";
    std::cout << "    Alice 报名周赛: " << sys.signup(alice, act) << "\n";
    std::cout << "    Carol 报名周赛: " << sys.signup(carol, act) << "\n";
    std::cout << "    Bob   报名周赛: " << sys.signup(bob, act) << "  (容量已满，进候补)\n";
    std::cout << "    Dave  报名周赛: " << sys.signup(dave, act) << "  (候补)\n";
    std::cout << "    [时间冲突检测] Alice 报名深夜编程局: " << sys.signup(alice, lateNight) << "\n";
    std::cout << "    [时间冲突检测] Alice 再报名时间重叠的夜谈会: " << sys.signup(alice, nightTalk) << "\n";

    std::cout << "[5] Carol 取消周赛报名，候补中活跃度最高者(Bob)被自动提拔:\n";
    std::cout << "    " << (sys.cancelSignup(carol, act) ? "已取消" : "失败") << "\n";
    auto wl = sys.activityWaitlistIds(act);
    std::cout << "    当前候补:";
    for (int id : wl) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?"); }
    std::cout << "\n";

    std::cout << "[6] 调整周赛容量 2→3：候补中的 Dave 被提拔:\n";
    std::cout << "    " << (sys.setActivityCapacity(act, 3) ? "已调整" : "失败") << "\n";

    std::cout << "[7] 好友关系与二度人脉推荐(Alice):\n";
    sys.addFriend(alice, bob);
    sys.addFriend(bob, dave);
    for (auto& r : sys.recommendFriends(alice)) {
        const Member* m = sys.findMember(r.memberId);
        std::cout << "    推荐 " << (m?m->name:"?") << " (共同好友 " << r.mutualFriends << " 人)\n";
    }
    std::cout << "    Alice↔Dave 最短关系距离: " << sys.friendDistance(alice, dave) << " 跳\n";
    std::cout << "    Alice↔Dave 最短社交路径:";
    for (int id : sys.friendPath(alice, dave)) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?")<<"(#"<<id<<")"; }
    std::cout << "\n";

    std::cout << "[8] 入社申请审批(优先级队列):\n";
    int appLow = sys.submitApplication(dave, coding, 0);
    int appHigh = sys.submitApplication(bob, coding, 5);
    std::cout << "    Dave 提交申请 #"<< appLow << "(优先级0)，Bob 提交 #"<< appHigh
              << "(优先级5)，待处理: "<< sys.pendingCount() << "\n";
    int next = sys.processNextApplication();
    std::cout << "    队首处理(应为 Bob): 申请 #"<< next << "\n";
    std::cout << "    审批通过: " << (sys.reviewApplication(next, true) ? "成功" : "失败") << "\n";

    std::cout << "[9] 公告广播:\n";
    std::cout << "    " << sys.broadcast(coding, alice, "今晚周赛，请准时参加！") << " 公告已发\n";
    std::cout << "    Carol 收到的公告: " << sys.announcementsForMember(carol).size() << " 条\n";
    std::cout << "    Dave 收到的公告: " << sys.announcementsForMember(dave).size() << " 条(未入社)\n";

    // 好友驱动活动推荐：Bob 是 Alice 的好友，Bob 参加了一个 Alice 未参加的活动
    int hike = sys.addActivity(coding, "周末徒步", "周日 08:00", 10);
    sys.signup(bob, hike);
    sys.addFriend(alice, dave);
    sys.signup(dave, hike);
    std::cout << "[10] 好友驱动活动推荐(Alice，好友 Bob/Dave 都参加了「周末徒步」):";
    for (auto& [aid, sc] : sys.recommendActivities(alice)) {
        const Activity* ac = sys.findActivity(aid);
        std::cout << " " << (ac?ac->name:"?") << "(好友热度=" << sc << ")";
    }
    std::cout << "\n";

    std::cout << "[11] 社团活跃度排行:";
    for (auto& [total, cid] : sys.clubLeaderboard(5)) {
        const Club* c = sys.findClub(cid);
        std::cout << " " << (c?c->name:"?") << "=" << total;
    }
    std::cout << "\n";

    std::cout << "[12] 撤销刚才 Dave 的徒步报名(命令模式，可 redo 重做):\n";
    std::cout << "    undo -> " << (sys.undo() ? "已撤销" : "无可撤销") << "\n";
    std::cout << "    redo -> " << (sys.redo() ? "已重做" : "无可重做") << "\n";

    std::cout << "[13] 统计活跃度在 [10,30] 的成员数: "
              << sys.rangeActive(10, 30) << "\n";
    std::cout << "[14] Alice 周赛签到: " << (sys.checkIn(alice, act) ? "成功，+10 分" : "失败") << "\n";
    std::cout << "     再次签到: " << (sys.checkIn(alice, act) ? "成功" : "拒绝重复签到") << "\n";
    auto stats = sys.statistics();
    std::cout << "     个人日程=" << sys.memberSchedule(alice).size() << " 场，累计签到=" << stats.checkIns << "\n";
    std::cout << "\n=== 演示结束 ===\n";
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    (void)argv;
    auto args = utf8Args(argc);
#else
    std::vector<std::string> args(argv, argv + argc);
#endif

    if (args.size() < 2) { usage(); return 0; }
    std::string cmd = args[1];
    auto a = [&](size_t i) -> std::string { return i < args.size() ? args[i] : ""; };
    if (cmd == "auth") {
        try {
            if (args.size() != 4 || (a(2) != "init" && a(2) != "reset")) throw std::invalid_argument("用法：club auth init|reset <username>");
            AuthStore auth;
            if (a(2) == "init" && auth.initialized()) throw std::runtime_error("已有账号，不能重复初始化");
            if (a(2) == "reset" && !auth.account(a(3))) throw std::runtime_error("账号不存在");
            auto password = readPassword("新密码（至少 10 字节）: ");
            if (password != readPassword("再次输入: ")) throw std::invalid_argument("两次密码不一致");
            auto error = a(2) == "init" ? auth.initialize(a(3), password) : auth.resetPassword(a(3), password);
            if (!error.empty()) throw std::runtime_error(error);
            std::cout << "账号已保存；可启动 gui 登录。\n";
            return 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }

    // 解析 tag: 前缀的参数为标签
    auto collectTags = [&](size_t start) {
        std::vector<std::string> tags;
        for (size_t i = start; i < args.size(); ++i) {
            const std::string& s = args[i];
            if (s.rfind("tag:", 0) == 0 && s.size() > 4) tags.push_back(s.substr(4));
        }
        return tags;
    };

    // 持久化策略：默认文件 club_data.txt 自动载入/保存，使多次调用保持状态。
    const std::string DEFAULT_SAVE = "club_data.txt";
    bool isDemo = (cmd == "demo");
    bool noAutoSave = isDemo || cmd == "help" || cmd == "load" || cmd == "save" || cmd == "gui";

    // 启动时自动载入默认存档（demo 除外）
    if (!isDemo && cmd != "help" && cmd != "load") {
        std::ifstream test(DEFAULT_SAVE);
        if (test.good()) {
            test.close();
            if (!sys.load(DEFAULT_SAVE)) {
                std::cerr << "存档损坏或格式不支持，已停止操作；原文件未改动。可用 load 导入有效存档。\n";
                return 1;
            }
        }
    }

    try {
        if (cmd == "demo") { runDemo(); return 0; }

        // ---- 成员 ----
        else if (cmd == "member") {
            std::string sub = a(2);
            if (sub == "add") {
                int id = sys.addMember(a(3), a(4), collectTags(5));
                std::cout << (id >= 0 ? ("已添加 id=" + std::to_string(id)) : "失败：重名") << "\n";
            } else if (sub == "remove") {
                std::cout << (sys.removeMember(parseInt(a(3))) ? "已删除" : "失败：拒绝或删除") << "\n";
            } else if (sub == "score") {
                sys.addScore(parseInt(a(3)), parseInt(a(4)));
                std::cout << "已调整\n";
            } else if (sub == "tag") {
                std::cout << (sys.addMemberTag(parseInt(a(3)), a(4)) ? "已打标签" : "失败") << "\n";
            } else if (sub == "untag") {
                std::cout << (sys.removeMemberTag(parseInt(a(3)), a(4)) ? "已删除标签" : "失败") << "\n";
            } else if (sub == "tags") {
                const Member* m = sys.findMember(parseInt(a(3)));
                if (!m) { std::cout << "未找到成员\n"; }
                else if (m->tags.empty()) std::cout << "  (无标签)\n";
                else {
                    std::cout << "  标签:";
                    for (auto& t : m->tags) std::cout << " " << t;
                    std::cout << "\n";
                }
            } else if (sub == "bytag") {
                auto ids = sys.membersWithTag(a(3));
                std::cout << "  " << a(3) << " (" << ids.size() << "):";
                for (int id : ids) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?")<<"(#"<<id<<")"; }
                std::cout << "\n";
            } else if (sub == "bytags") {
                std::string mode = a(3);
                std::vector<std::string> tags;
                for (size_t i = 4; i < args.size(); ++i) tags.push_back(args[i]);
                auto ids = (mode == "all") ? sys.membersWithAllTags(tags) : sys.membersWithAnyTag(tags);
                std::cout << "  " << mode << " (" << ids.size() << "):";
                for (int id : ids) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?")<<"(#"<<id<<")"; }
                std::cout << "\n";
            } else if (sub == "alltags") {
                auto ts = sys.allTags();
                std::cout << "  所有标签(" << ts.size() << "):";
                for (auto& t : ts) std::cout << " " << t;
                std::cout << "\n";
            } else if (sub == "import") {
                std::ifstream in(a(3));
                if (!in) { std::cout << "失败：无法打开文件\n"; }
                else {
                    std::stringstream ss; ss << in.rdbuf();
                    auto r = sys.importMembers(ss.str());
                    std::cout << "导入完成: 新增=" << r.added << " 重名跳过=" << r.skippedDup
                              << " 格式错误=" << r.skippedInvalid << "\n";
                }
            } else if (sub == "list") printMembers();
            else if (sub == "find") {
                int id = sys.findMemberByName(a(3));
                std::cout << (id >= 0 ? ("id=" + std::to_string(id)) : "未找到") << "\n";
            }
        }
        // ---- 社团 ----
        else if (cmd == "club") {
            std::string sub = a(2);
            if (sub == "create") {
                int id = sys.createClub(a(3), a(4), a(5), a(6));
                std::cout << (id >= 0 ? ("已创建 id=" + std::to_string(id)) : "失败：重名") << "\n";
            } else if (sub == "sub") {
                int id = sys.createSubClub(parseInt(a(3)), a(4), a(5), a(6), a(7));
                std::cout << (id >= 0 ? ("已创建子部门 id=" + std::to_string(id)) : "失败：父不存在或重名") << "\n";
            } else if (sub == "org") {
                auto lines = sys.orgTreeLines();
                if (lines.empty()) std::cout << "  (暂无社团)\n";
                for (auto& l : lines) std::cout << "  " << l << "\n";
            } else if (sub == "remove") {
                std::cout << (sys.removeClub(parseInt(a(3))) ? "已删除" : "失败") << "\n";
            } else if (sub == "join") {
                std::cout << (sys.joinClub(parseInt(a(3)), parseInt(a(4))) ? "已入社" : "失败") << "\n";
            } else if (sub == "leave") {
                std::cout << (sys.leaveClub(parseInt(a(3)), parseInt(a(4))) ? "已退社" : "失败") << "\n";
            } else if (sub == "list") {
                for (const auto& c : sys.clubs()) {
                    if (!c.active) continue;
                    std::cout << "  #" << c.id << " " << c.name << " [" << c.category
                              << "] 成员数=" << c.memberIds.size();
                    if (c.parentClub >= 0) std::cout << " (父#" << c.parentClub << ")";
                    std::cout << "\n";
                }
            }
        }
        // ---- 活动 ----
        else if (cmd == "activity") {
            std::string sub = a(2);
            if (sub == "checkin") {
                bool ok = sys.checkIn(parseInt(a(3)), parseInt(a(4)));
                std::cout << (ok ? "签到成功，活跃度 +10" : "签到失败：必须正取、未签到且活动未结束") << "\n";
                if (!ok) return 1;
            } else if (sub == "close") {
                bool ok = sys.closeActivity(parseInt(a(3)));
                std::cout << (ok ? "活动已结束" : "结束失败") << "\n";
                if (!ok) return 1;
            } else if (sub == "add") {
                int id = sys.addActivity(parseInt(a(3)), a(4), a(5), parseInt(a(6)));
                if (id < 0) throw std::invalid_argument("活动参数无效或重名");
                std::cout << "已添加 id=" << std::to_string(id) << "\n";
            } else if (sub == "addtimed") {
                int id = sys.addActivity(parseInt(a(3)), a(4), "", parseInt(a(5)),
                                         collectTags(6), parseInt(a(6)),
                                         parseInt(a(7)), parseInt(a(8)));
                std::cout << (id >= 0 ? ("已添加(带时间段) id=" + std::to_string(id)) : "失败") << "\n";
            } else if (sub == "remove") {
                std::cout << (sys.removeActivity(parseInt(a(3))) ? "已删除" : "失败") << "\n";
            } else if (sub == "signup") {
                std::cout << sys.signup(parseInt(a(3)), parseInt(a(4))) << "\n";
            } else if (sub == "cancel") {
                std::cout << (sys.cancelSignup(parseInt(a(3)), parseInt(a(4))) ? "已取消" : "失败") << "\n";
            } else if (sub == "cap") {
                std::cout << (sys.setActivityCapacity(parseInt(a(3)), parseInt(a(4))) ? "已调整容量" : "失败") << "\n";
            } else if (sub == "list") {
                for (const auto& ac : sys.activities()) {
                    if (!ac.active) continue;
                    std::cout << "  #" << ac.id << " " << ac.name << " @ " << ac.time
                              << " 容量=" << ac.capacity << " 已报名=" << ac.signedUp.size();
                    if (ac.timed()) std::cout << " [第" << ac.day << "天 " << ac.startMin << "-" << ac.endMin << "分]";
                    std::cout << "\n";
                }
            } else if (sub == "waitlist") {
                auto wl = sys.activityWaitlistIds(parseInt(a(3)));
                std::cout << "候补(" << wl.size() << "):";
                for (int id : wl) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?")<<"(#"<<id<<")"; }
                std::cout << "\n";
            }
        }
        // ---- 申请 ----
        else if (cmd == "app") {
            std::string sub = a(2);
            if (sub == "withdraw") {
                bool ok = sys.withdrawApplication(parseInt(a(3)), parseInt(a(4)));
                std::cout << (ok ? "已撤回" : "撤回失败") << "\n";
                if (!ok) return 1;
            } else if (sub == "submit") {
                int prio = a(5).empty() ? 0 : parseInt(a(5));
                int id = sys.submitApplication(parseInt(a(3)), parseInt(a(4)), prio);
                std::cout << (id >= 0 ? ("已提交 id=" + std::to_string(id)) : "失败") << "\n";
            } else if (sub == "process") {
                int id = sys.processNextApplication();
                std::cout << (id >= 0 ? ("处理优先级最高申请 #" + std::to_string(id)) : "无待审") << "\n";
            } else if (sub == "review") {
                if (a(4) != "approve" && a(4) != "reject") throw std::invalid_argument("使用 approve 或 reject");
                bool ok = sys.reviewApplication(parseInt(a(3)), a(4) == "approve");
                std::cout << (ok ? "已审批" : "失败") << "\n";
            } else if (sub == "pending") {
                std::cout << "待审数量: " << sys.pendingCount() << "\n";
            }
        }
        // ---- 好友 ----
        else if (cmd == "friend") {
            std::string sub = a(2);
            if (sub == "add") { sys.addFriend(parseInt(a(3)), parseInt(a(4))); std::cout << "已添加好友\n"; }
            else if (sub == "remove") { std::cout << (sys.removeFriend(parseInt(a(3)), parseInt(a(4))) ? "已删除" : "失败") << "\n"; }
            else if (sub == "recommend") {
                auto recs = sys.recommendFriends(parseInt(a(3)));
                for (auto& r : recs) {
                    const Member* m = sys.findMember(r.memberId);
                    std::cout << "  推荐 " << (m?m->name:"?") << " 共同好友=" << r.mutualFriends << "\n";
                }
                if (recs.empty()) std::cout << "  (无推荐)\n";
            } else if (sub == "distance") {
                std::cout << "距离: " << sys.friendDistance(parseInt(a(3)), parseInt(a(4))) << " 跳\n";
            } else if (sub == "path") {
                auto p = sys.friendPath(parseInt(a(3)), parseInt(a(4)));
                if (p.empty()) std::cout << "  不连通\n";
                else {
                    std::cout << "  路径(" << (p.size()-1) << " 跳):";
                    for (int id : p) { const Member* m = sys.findMember(id); std::cout << " " << (m?m->name:"?")<<"(#"<<id<<")"; }
                    std::cout << "\n";
                }
            } else if (sub == "recact") {
                auto recs = sys.recommendActivities(parseInt(a(3)));
                if (recs.empty()) std::cout << "  (无推荐)\n";
                for (auto& [aid, sc] : recs) {
                    const Activity* ac = sys.findActivity(aid);
                    std::cout << "  推荐 " << (ac?ac->name:"?") << " 好友热度=" << sc << "\n";
                }
            }
        }
        // ---- 排行 ----
        else if (cmd == "rank") {
            std::string sub = a(2);
            if (sub == "leaderboard") {
                size_t k = a(3).empty() ? 10 : parseCount(a(3));
                for (auto& [score, id] : sys.leaderboard(k)) {
                    const Member* m = sys.findMember(id);
                    std::cout << "  " << (m?m->name:"?") << " 活跃度=" << score << "\n";
                }
            } else if (sub == "member") {
                std::cout << "排名: " << sys.memberRank(parseInt(a(3))) << "\n";
            } else if (sub == "range") {
                std::cout << "区间人数: " << sys.rangeActive(parseInt(a(3)), parseInt(a(4))) << "\n";
            } else if (sub == "clubleader") {
                size_t k = a(3).empty() ? 10 : parseCount(a(3));
                for (auto& [total, cid] : sys.clubLeaderboard(k)) {
                    const Club* c = sys.findClub(cid);
                    std::cout << "  " << (c?c->name:"?") << " 总活跃度=" << total << "\n";
                }
            }
        }
        // ---- 公告 ----
        else if (cmd == "announce") {
            std::string sub = a(2);
            if (sub == "broadcast") {
                std::string msg;
                for (size_t i = 5; i < args.size(); ++i) { if (i > 5) msg += " "; msg += args[i]; }
                int id = sys.broadcast(parseInt(a(3)), parseInt(a(4)), msg);
                std::cout << (id >= 0 ? ("已广播 id=" + std::to_string(id)) : "失败") << "\n";
            } else if (sub == "club") {
                auto list = sys.announcementsOf(parseInt(a(3)));
                std::cout << "  公告(" << list.size() << "):\n";
                for (auto& an : list) {
                    const Member* s = sys.findMember(an.senderId);
                    std::cout << "    #" << an.id << " 来自" << (s?s->name:"?") << ": " << an.message << "\n";
                }
            } else if (sub == "mine") {
                auto list = sys.announcementsForMember(parseInt(a(3)));
                std::cout << "  我能收到(" << list.size() << "):\n";
                for (auto& an : list) {
                    const Club* c = sys.findClub(an.clubId);
                    std::cout << "    [" << (c?c->name:"?") << "] " << an.message << "\n";
                }
            }
        }
        else if (cmd == "stats") {
            auto st = sys.statistics();
            std::cout << "成员=" << st.members << " 社团=" << st.clubs << " 活动=" << st.activities
                      << " 已结束=" << st.closedActivities << "\n正取=" << st.registrations << " 候补=" << st.waiting
                      << " 签到=" << st.checkIns << " 待审=" << st.pending << " 总积分=" << st.totalScore << "\n";
        } else if (cmd == "schedule") {
            int mid = parseInt(a(2));
            if (!sys.findMember(mid)) throw std::invalid_argument("成员不存在");
            auto list = sys.memberSchedule(mid);
            if (list.empty()) std::cout << "暂无参与的活动\n";
            for (int id : list) {
                const auto& ac = *sys.findActivity(id);
                bool signedUp = std::find(ac.signedUp.begin(), ac.signedUp.end(), mid) != ac.signedUp.end();
                std::cout << '#' << id << ' ' << ac.name << ' ' << (ac.closed ? "已结束" : signedUp ? "正取" : "候补")
                          << " day=" << ac.day << ' ' << ac.startMin << '-' << ac.endMin << "\n";
            }
        } else if (cmd == "export") {
            std::string csv, path;
            if (a(2) == "members") { csv = sys.exportMembersCsv(); path = a(3); }
            else if (a(2) == "roster") { csv = sys.exportRosterCsv(parseInt(a(3))); path = a(4); }
            if (csv.empty() || path.empty() || !club_io::atomicWrite(path, csv)) throw std::runtime_error("导出失败");
            std::cout << "已导出 " << path << "\n";
        }
        // ---- 其他 ----
        else if (cmd == "undo") { std::cout << (sys.undo() ? "已撤销" : "无可撤销") << "\n"; }
        else if (cmd == "redo") { std::cout << (sys.redo() ? "已重做" : "无可重做") << "\n"; }
        else if (cmd == "save") { if (!sys.save(a(2))) throw std::runtime_error("保存失败"); std::cout << "已保存\n"; }
        else if (cmd == "load") {
            // 导入：载入指定存档并设为当前默认存档，后续命令自动接续
            if (!sys.load(a(2)) || !sys.save(DEFAULT_SAVE)) throw std::runtime_error("导入或保存失败，原存档未被覆盖");
            std::cout << "已载入并设为当前存档\n";
        }
        else if (cmd == "gui") {
            // 启动局域网多人共用 Web 界面（长驻服务器，内部自行载入/保存 club_data.txt）
            startGui(sys);
            return 0;
        }
        else { usage(); }
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }

    // 退出时自动保存默认存档（demo/help/load/save 除外）
    if (!noAutoSave && !sys.save(DEFAULT_SAVE)) { std::cerr << "保存失败\n"; return 1; }
    return 0;
}
