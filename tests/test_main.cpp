// ClubManager 单元测试：覆盖各数据结构与系统层逻辑。
// 直接编译运行，全部断言通过则打印 "ALL TESTS PASSED (n checks)"。
#include "system.h"
#include "hash_map.h"
#include "avl.h"
#include "heap.h"
#include "containers.h"
#include "friend_graph.h"

#include <iostream>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstdio>

static int g_checks = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { ++g_fail; std::cerr << "  [FAIL] " << __LINE__ << ": " #cond "\n"; } \
} while (0)

// ---------------- HashMap ----------------
static void testHashMap() {
    std::cout << "[测试] HashMap 链地址哈希表\n";
    StringIntMap m(4);
    CHECK(m.empty());
    m.put("Alice", 1);
    m.put("Bob", 2);
    m.put("Carol", 3);
    CHECK(m.size() == 3);
    int v = 0;
    CHECK(m.get("Bob", v) && v == 2);
    CHECK(m.contains("Alice"));
    CHECK(!m.contains("Dave"));

    // 更新已有键
    m.put("Bob", 20);
    CHECK(m.get("Bob", v) && v == 20);
    CHECK(m.size() == 3);

    // 触发 rehash（大量插入）
    for (int i = 0; i < 100; ++i) m.put("k" + std::to_string(i), i);
    CHECK(m.size() == 103);
    CHECK(m.get("k50", v) && v == 50);

    // 删除
    CHECK(m.remove("Alice"));
    CHECK(!m.contains("Alice"));
    CHECK(m.size() == 102);
    CHECK(!m.remove("Alice"));  // 二次删除
}

// ---------------- AVL ----------------
static void testAVL() {
    std::cout << "[测试] AVL 平衡树 (score,id)\n";
    AVL t;
    t.insert(10, 1);
    t.insert(20, 2);
    t.insert(30, 3);   // 触发左旋
    t.insert(5, 4);
    t.insert(40, 5);   // 再触发
    CHECK(t.size() == 5);

    // 同一 score 不同 id 视为不同键
    t.insert(20, 99);
    CHECK(t.size() == 6);
    CHECK(t.contains(20, 99));
    CHECK(!t.contains(20, 100));

    // Top-K：按 score 降序（同分 id 大者在前，因关键字为 (score,id)）
    auto top = t.topK(3);
    CHECK(top.size() == 3);
    CHECK(top[0].first == 40);
    CHECK(top[1].first == 30);
    CHECK(top[2].first == 20 && top[2].second == 99);

    // rankOf：score 最高者 rank=0；同分时 id 大者 rank 靠前
    CHECK(t.rankOf(40, 5) == 0);
    CHECK(t.rankOf(30, 3) == 1);
    // 严格大于 (20,2) 的有 (40,5),(30,3),(20,99) 共 3 个
    int r = t.rankOf(20, 2);
    CHECK(r == 3);

    // rangeCount：score 在 [20,30]
    int c = t.rangeCount(20, 30);
    CHECK(c == 3);  // (20,2),(20,99),(30,3)

    // inorder 升序
    auto io = t.inorder();
    for (size_t i = 1; i < io.size(); ++i) {
        CHECK(io[i-1] < io[i]);  // 严格升序
    }

    // 删除后平衡保持、TopK 正确
    t.remove(20, 99);
    CHECK(t.size() == 5);
    auto top2 = t.topK(10);
    CHECK(top2.size() == 5);
    CHECK(top2[0].first == 40);

    // 删空
    for (auto& p : top2) t.remove(p.first, p.second);
    CHECK(t.empty());
}

// ---------------- BinaryHeap ----------------
static void testHeap() {
    std::cout << "[测试] BinaryHeap 最大堆\n";
    BinaryHeap<int> h;
    int data[] = {5, 3, 8, 1, 9, 2, 7};
    for (int x : data) h.push(x);
    CHECK(h.size() == 7);
    CHECK(h.top() == 9);

    int prev = 100;
    while (!h.empty()) {
        int x = h.top(); h.pop();
        CHECK(x <= prev);  // 降序弹出
        prev = x;
    }
    CHECK(h.empty());

    // 对 pair<score,id> 默认 less 比较（score 优先）
    BinaryHeap<std::pair<int,int>> hp;
    hp.push({5, 100});
    hp.push({9, 1});
    hp.push({5, 2});
    CHECK(hp.top().first == 9);
}

// ---------------- Stack / Queue ----------------
static void testContainers() {
    std::cout << "[测试] Stack 顺序栈 / Queue 循环队列\n";
    Stack<int> s;
    CHECK(s.empty());
    s.push(1); s.push(2); s.push(3);
    CHECK(s.size() == 3);
    CHECK(s.top() == 3);
    s.pop();
    CHECK(s.top() == 2);

    Queue<int> q(2);  // 小容量触发扩容
    CHECK(q.empty());
    for (int i = 1; i <= 5; ++i) q.push(i);  // 超过初值容量
    CHECK(q.size() == 5);
    CHECK(q.front() == 1);
    q.pop(); q.pop();
    CHECK(q.front() == 3);
    // 环绕后再入队出队
    q.pop(); q.pop(); q.pop();
    CHECK(q.empty());
}

// ---------------- FriendGraph ----------------
static void testFriendGraph() {
    std::cout << "[测试] FriendGraph 无向图 + BFS\n";
    FriendGraph g;
    g.addFriend(1, 2);
    g.addFriend(2, 3);
    g.addFriend(3, 4);
    g.addFriend(1, 3);
    CHECK(g.areFriends(1, 2));
    CHECK(g.areFriends(2, 1));  // 无向
    CHECK(!g.areFriends(1, 4));
    CHECK(g.edgeCount() == 4);

    // BFS 距离
    CHECK(g.friendDistance(1, 4) == 2);  // 1-3-4
    CHECK(g.friendDistance(1, 1) == 0);
    // 不连通
    g.addFriend(10, 11);
    CHECK(g.friendDistance(1, 10) == -1);

    // 二度人脉推荐：为 1 推荐 4（通过 3，可能还有 2 已是好友）
    auto recs = g.recommend(1);
    bool found4 = false;
    for (auto& r : recs) if (r.memberId == 4) { found4 = true; CHECK(r.mutualFriends >= 1); }
    CHECK(found4);

    // 删除（此时含 (10,11) 那条边）
    CHECK(g.removeFriend(1, 2));
    CHECK(!g.areFriends(1, 2));
    CHECK(g.edgeCount() == 4);
}

// ---------------- ClubSystem 成员/排行 ----------------
static void testSystemMembers() {
    std::cout << "[测试] 成员与 AVL 排行\n";
    ClubSystem sys;
    int a = sys.addMember("Alice", "2022");
    int b = sys.addMember("Bob", "2022");
    int c = sys.addMember("Carol", "2021");
    CHECK(a >= 0 && b >= 0 && c >= 0);
    CHECK(sys.addMember("Alice", "x") == -1);  // 重名

    sys.addScore(c, 30);
    sys.addScore(a, 20);
    sys.addScore(b, 10);
    sys.addScore(a, -100);  // 负分被夹到 0 -> 20-... a: 20 -> max(0,-80)=0
    CHECK(sys.findMember(a)->score == 0);

    // 排行榜
    auto lb = sys.leaderboard(3);
    CHECK(lb.size() == 3);
    CHECK(lb[0].first == 30);  // Carol
    CHECK(sys.memberRank(c) == 0);
    CHECK(sys.memberRank(a) == 2);  // 最低
    // 此时分数: Carol=30, Alice=0, Bob=10
    CHECK(sys.rangeActive(0, 30) == 3);
    CHECK(sys.rangeActive(5, 25) == 1);   // 仅 Bob(10)
    CHECK(sys.rangeActive(31, 40) == 0);

    // 删除保护：先让 Carol 报个名
    CHECK(sys.removeMember(b) == true);  // Bob 无报名无申请，可删
    CHECK(sys.findMember(b) == nullptr);
    // 再删 Alice（有 activity 保护后面测）
}

// ---------------- ClubSystem 社团/活动/候补 ----------------
static void testSystemClubsActivities() {
    std::cout << "[测试] 社团/活动/候补堆\n";
    ClubSystem sys;
    int a = sys.addMember("Alice", "2022");
    int b = sys.addMember("Bob", "2022");
    int c = sys.addMember("Carol", "2021");
    int d = sys.addMember("Dave", "2023");
    sys.addScore(c, 30); sys.addScore(a, 20); sys.addScore(b, 10); sys.addScore(d, 5);

    int club = sys.createClub("编程社", "学术", "2020", "desc");
    CHECK(club >= 0);
    CHECK(sys.createClub("编程社", "", "", "") == -1);  // 重名
    sys.joinClub(club, a);
    sys.joinClub(club, c);
    CHECK(sys.joinClub(club, a) == false);  // 重复入社

    // 删除社团：有在册成员则拒绝
    CHECK(sys.removeClub(club) == false);
    sys.leaveClub(club, a);
    sys.leaveClub(club, c);
    CHECK(sys.removeClub(club) == true);

    // 重新建社团测候补
    club = sys.createClub("编程社", "学术", "2020", "desc");
    int act = sys.addActivity(club, "周赛", "周六", 2);
    CHECK(act >= 0);

    std::string s1 = sys.signup(a, act);
    std::string s2 = sys.signup(c, act);
    std::string s3 = sys.signup(b, act);  // 满员→候补
    std::string s4 = sys.signup(d, act);  // 候补
    CHECK(s1.find("成功") != std::string::npos);
    CHECK(s3.find("候补") != std::string::npos);
    CHECK(s4.find("候补") != std::string::npos);

    // 重复报名被拒
    CHECK(sys.signup(a, act).find("已报名") != std::string::npos);

    // 候补按活跃度：Bob(10) > Dave(5)，Bob 应在前
    auto wl = sys.activityWaitlistIds(act);
    CHECK(wl.size() == 2);
    CHECK(wl[0] == b);
    CHECK(wl[1] == d);

    // Carol 取消正取名额 → 自动提拔 Bob
    CHECK(sys.cancelSignup(c, act) == true);
    auto wl2 = sys.activityWaitlistIds(act);
    CHECK(wl2.size() == 1);
    CHECK(wl2[0] == d);  // Dave 仍在候补
    // Carol 已不在 signedUp
    CHECK(std::find(sys.findActivity(act)->signedUp.begin(),
                    sys.findActivity(act)->signedUp.end(), c)
          == sys.findActivity(act)->signedUp.end());
    // Bob 已被提拔进 signedUp
    CHECK(std::find(sys.findActivity(act)->signedUp.begin(),
                    sys.findActivity(act)->signedUp.end(), b)
          != sys.findActivity(act)->signedUp.end());

    // 删除成员保护：Dave 在候补，删他应被拒
    CHECK(sys.removeMember(d) == false);
    // 取消 Dave 候补后可删
    sys.cancelSignup(d, act);
    CHECK(sys.removeMember(d) == true);
}

// ---------------- ClubSystem 申请审批队列 ----------------
static void testSystemApplications() {
    std::cout << "[测试] 入社申请 FIFO 队列\n";
    ClubSystem sys;
    int a = sys.addMember("Alice", "2022");
    int b = sys.addMember("Bob", "2022");
    int club = sys.createClub("摄影社", "文艺", "2021", "");
    int club2 = sys.createClub("棋社", "文娱", "2021", "");

    int app1 = sys.submitApplication(a, club);
    int app2 = sys.submitApplication(b, club2);
    CHECK(app1 >= 0 && app2 >= 0);
    CHECK(sys.pendingCount() == 2);

    // processNextApplication 按提交顺序返回
    int first = sys.processNextApplication();
    CHECK(first == app1);
    CHECK(sys.processNextApplication() == app1); // 查看不消费队列
    CHECK(sys.pendingCount() == 2);
    CHECK(sys.reviewApplication(app1, false));
    // 再处理第二个
    int second = sys.processNextApplication();
    CHECK(second == app2);
    CHECK(sys.reviewApplication(app2, false));
    // 队列空
    CHECK(sys.processNextApplication() == -1);
    CHECK(sys.pendingCount() == 0);

    // 重新提交并审批
    app1 = sys.submitApplication(a, club);
    CHECK(sys.reviewApplication(app1, true) == true);
    // 通过后 a 应已入 club
    auto& members = sys.findClub(club)->memberIds;
    CHECK(std::find(members.begin(), members.end(), a) != members.end());

    // 拒绝
    app2 = sys.submitApplication(b, club2);
    CHECK(sys.reviewApplication(app2, false) == true);
    auto& m2 = sys.findClub(club2)->memberIds;
    CHECK(std::find(m2.begin(), m2.end(), b) == m2.end());
}

// ---------------- 撤销 ----------------
static void testUndo() {
    std::cout << "[测试] 撤销（命令模式栈）\n";
    ClubSystem sys;
    int a = sys.addMember("Alice", "2022");
    CHECK(sys.findMember(a) != nullptr);
    CHECK(sys.undo() == true);   // 撤销添加成员
    CHECK(sys.findMember(a) == nullptr);

    // 撤销 addScore
    int b = sys.addMember("Bob", "2022");
    sys.addScore(b, 50);
    CHECK(sys.findMember(b)->score == 50);
    sys.undo();
    CHECK(sys.findMember(b)->score == 0);

    // 撤销 addFriend
    int c = sys.addMember("Carol", "2022");
    sys.addFriend(b, c);
    CHECK(sys.friendDistance(b, c) == 1);
    sys.undo();
    CHECK(sys.friendDistance(b, c) == -1);

    // 空栈撤销
    ClubSystem s2;
    CHECK(s2.undo() == false);
}

// ---------------- 持久化 ----------------
static void testPersistence() {
    std::cout << "[测试] 保存/载入往返\n";
    ClubSystem sys;
    int a = sys.addMember("Alice", "2022");
    int b = sys.addMember("Bob", "2021");
    int c = sys.addMember("Carol", "2023");
    sys.addScore(b, 30);
    sys.addScore(a, 15);
    int club = sys.createClub("编程社", "学术", "2020", "desc");
    sys.joinClub(club, a);
    sys.joinClub(club, b);
    int act = sys.addActivity(club, "周赛", "周六", 1);
    sys.signup(a, act);       // 占满 capacity=1
    sys.signup(c, act);       // 候补
    sys.addFriend(a, b);
    int app = sys.submitApplication(c, club);
    CHECK(app >= 0);

    const char* path = "_test_save.txt";
    CHECK(sys.save(path) == true);

    ClubSystem sys2;
    CHECK(sys2.load(path) == true);

    // 校验成员
    CHECK(sys2.findMemberByName("Alice") == a);
    CHECK(sys2.findMemberByName("Bob") == b);
    CHECK(sys2.findMemberByName("Carol") == c);
    CHECK(sys2.findMember(a)->score == 15);
    CHECK(sys2.findMember(b)->score == 30);

    // 社团成员
    auto& mem = sys2.findClub(club)->memberIds;
    CHECK(std::find(mem.begin(), mem.end(), a) != mem.end());
    CHECK(std::find(mem.begin(), mem.end(), b) != mem.end());

    // 活动候补：Carol 应在候补
    auto wl = sys2.activityWaitlistIds(act);
    CHECK(wl.size() == 1);
    CHECK(wl[0] == c);
    // 正取名额也应持久化（Alice）
    auto signedIds = sys2.activities()[act].signedUp;
    CHECK(std::find(signedIds.begin(), signedIds.end(), a) != signedIds.end());

    // 好友
    CHECK(sys2.friendDistance(a, b) == 1);

    // 排行一致
    auto lb = sys2.leaderboard(3);
    CHECK(lb[0].first == 30);  // Bob

    // 待审队列恢复（1 个 pending）
    CHECK(sys2.pendingCount() == 1);
    std::remove(path);
}

// ---------------- 标签倒排索引 ----------------
static void testTagIndex() {
    std::cout << "[测试] 标签倒排索引 (AND/OR)\n";
    ClubSystem sys;
    int a = sys.addMember("Alice", "2022", {"Python", "算法"});
    sys.addMember("Bob", "2022", {"C++", "算法"});
    int c = sys.addMember("Carol", "2021", {"Python"});
    CHECK(sys.membersWithTag("Python").size() == 2);
    CHECK(sys.membersWithTag("算法").size() == 2);
    auto both = sys.membersWithAllTags({"Python", "算法"});
    CHECK(both.size() == 1);
    CHECK(both[0] == a);
    CHECK(sys.membersWithAnyTag({"Python", "C++"}).size() == 3);
    CHECK(sys.allTags().size() >= 3);

    // 打/删标签 + 撤销
    sys.addMemberTag(c, "运维");
    CHECK(sys.membersWithTag("运维").size() == 1);
    sys.removeMemberTag(c, "运维");
    CHECK(sys.membersWithTag("运维").empty());
    sys.addMemberTag(a, "临时");
    CHECK(sys.membersWithTag("临时").size() == 1);
    sys.undo();
    CHECK(sys.membersWithTag("临时").empty());
}

// ---------------- 社团层级部门树 ----------------
static void testOrgTree() {
    std::cout << "[测试] 社团层级部门树 (DFS)\n";
    ClubSystem sys;
    int root = sys.createClub("总会", "综合", "2020", "root");
    int sub1 = sys.createSubClub(root, "技术部", "学术", "2021", "tech");
    sys.createSubClub(root, "文体部", "文艺", "2021", "art");
    int leaf = sys.createSubClub(sub1, "算法组", "学术", "2022", "algo");
    auto lines = sys.orgTreeLines();
    CHECK(lines.size() == 4);
    // 有子部门的社团不可删
    CHECK(sys.removeClub(root) == false);
    CHECK(sys.findClub(root) != nullptr);
    // 叶子部门可删
    CHECK(sys.removeClub(leaf) == true);
    CHECK(sys.findClub(leaf) == nullptr);
    // 撤销删除叶子
    sys.undo();
    CHECK(sys.findClub(leaf) != nullptr);
    // 撤销创建子部门
    sys.undo();  // 撤销 removeClub(leaf)? 不——上面 undo 已撤销删除；再 undo 撤销 createSubClub(leaf)
    CHECK(sys.findClub(leaf) == nullptr);
}

// ---------------- 活动时间冲突 ----------------
static void testTimeConflict() {
    std::cout << "[测试] 活动时间冲突检测\n";
    ClubSystem sys;
    int c = sys.createClub("社", "学术", "2020", "");
    int a = sys.addMember("Alice", "2022");
    sys.addMemberTag(a, "x");  // 冲突测试先满足活动报名资格
    int t1 = sys.addActivity(c, "A", "", 5, {"x"}, 5, 18 * 60, 19 * 60);
    int t2 = sys.addActivity(c, "B", "", 5, {}, 5, 18 * 60 + 30, 19 * 60 + 30);  // 重叠
    int t3 = sys.addActivity(c, "C", "", 5, {}, 6, 10 * 60, 11 * 60);             // 不同天
    CHECK(sys.signup(a, t1).find("成功") != std::string::npos);
    CHECK(sys.signup(a, t2).find("冲突") != std::string::npos);
    CHECK(sys.signup(a, t3).find("成功") != std::string::npos);
}

// ---------------- 容量调整 ----------------
static void testCapacityAdjust() {
    std::cout << "[测试] 容量调整自动补位/降级\n";
    ClubSystem sys;
    int c = sys.createClub("社", "学术", "2020", "");
    int a = sys.addMember("A", "2022");
    int b = sys.addMember("B", "2022");
    int cc = sys.addMember("C", "2022");
    int d = sys.addMember("D", "2022");
    int act = sys.addActivity(c, "赛事", "周六", 1);
    sys.signup(a, act);
    sys.signup(b, act);   // 候补
    sys.signup(cc, act);  // 候补
    sys.signup(d, act);   // 候补
    sys.addScore(b, 50);
    sys.addScore(cc, 30);
    // 扩容 1→3：按活跃度提拔 b, c
    CHECK(sys.setActivityCapacity(act, 3) == true);
    auto wl = sys.activityWaitlistIds(act);
    CHECK(std::find(wl.begin(), wl.end(), b) == wl.end());
    CHECK(std::find(wl.begin(), wl.end(), cc) == wl.end());
    CHECK(wl.size() == 1);  // 只剩 d
    // 缩容 3→1：正取名额中活跃度最低的降级到候补
    CHECK(sys.setActivityCapacity(act, 1) == true);
    auto& actRef = sys.activities()[act];
    CHECK(actRef.signedUp.size() == 1);
    CHECK(actRef.signedUp[0] == b); // 当前积分最高者保留
    auto wl2 = sys.activityWaitlistIds(act);
    CHECK(wl2.size() == 3);
    CHECK(std::find(wl2.begin(), wl2.end(), a) != wl2.end());
    CHECK(std::find(wl2.begin(), wl2.end(), cc) != wl2.end());
    CHECK(std::find(wl2.begin(), wl2.end(), d) != wl2.end());
}

// ---------------- 申请优先级队列 ----------------
static void testPriorityApp() {
    std::cout << "[测试] 申请优先级队列\n";
    ClubSystem sys;
    int a = sys.addMember("A", "2022");
    int b = sys.addMember("B", "2022");
    int c = sys.addMember("C", "2022");
    int club = sys.createClub("社", "学术", "2020", "");
    int appLow = sys.submitApplication(a, club, 0);
    int appHigh = sys.submitApplication(b, club, 10);
    int appMid = sys.submitApplication(c, club, 5);
    CHECK(sys.pendingCount() == 3);
    // 队首 = 优先级最高者
    CHECK(sys.processNextApplication() == appHigh);
    CHECK(sys.reviewApplication(appHigh, false));
    CHECK(sys.processNextApplication() == appMid);
    CHECK(sys.reviewApplication(appMid, false));
    CHECK(sys.processNextApplication() == appLow);
    CHECK(sys.reviewApplication(appLow, false));
    CHECK(sys.pendingCount() == 0);

    // 提交可撤销
    ClubSystem s2;
    int x = s2.addMember("X", "2022");
    int cl = s2.createClub("C", "学术", "2020", "");
    s2.submitApplication(x, cl, 3);
    CHECK(s2.pendingCount() == 1);
    s2.undo();
    CHECK(s2.pendingCount() == 0);
}

// ---------------- 最短社交路径 ----------------
static void testFriendPath() {
    std::cout << "[测试] 最短社交路径 (BFS 回溯)\n";
    ClubSystem sys;
    int a = sys.addMember("A", "2022");
    int b = sys.addMember("B", "2022");
    int c = sys.addMember("C", "2022");
    int d = sys.addMember("D", "2022");
    sys.addFriend(a, b);
    sys.addFriend(b, c);
    sys.addFriend(c, d);
    CHECK(sys.friendDistance(a, d) == 3);
    auto p = sys.friendPath(a, d);
    CHECK(p.size() == 4);
    CHECK(p.front() == a);
    CHECK(p.back() == d);
    // 撤销 addFriend(c,d) → 不连通
    sys.undo();
    CHECK(sys.friendDistance(a, d) == -1);
    CHECK(sys.friendPath(a, d).empty());
}

// ---------------- 社团活跃度排行 ----------------
static void testClubLeaderboard() {
    std::cout << "[测试] 社团活跃度排行\n";
    ClubSystem sys;
    int a = sys.addMember("A", "2022");
    int b = sys.addMember("B", "2022");
    sys.addScore(a, 100);
    sys.addScore(b, 50);
    int c1 = sys.createClub("高活跃", "学术", "2020", "");
    int c2 = sys.createClub("低活跃", "学术", "2020", "");
    sys.joinClub(c1, a);
    sys.joinClub(c2, b);
    auto lb = sys.clubLeaderboard(5);
    CHECK(lb.size() == 2);
    CHECK(lb[0].first == 100);
    CHECK(lb[0].second == c1);
    CHECK(lb[1].first == 50);
}

// ---------------- 公告广播 ----------------
static void testAnnounce() {
    std::cout << "[测试] 公告广播\n";
    ClubSystem sys;
    int c = sys.createClub("社", "学术", "2020", "");
    int a = sys.addMember("A", "2022");
    int b = sys.addMember("B", "2022");
    sys.joinClub(c, a);
    int id = sys.broadcast(c, a, "开会了");
    CHECK(id >= 0);
    CHECK(sys.announcementsForMember(a).size() == 1);
    CHECK(sys.announcementsForMember(b).size() == 0);  // b 不在社内
    CHECK(sys.announcementsOf(c).size() == 1);
    // 撤销广播
    sys.undo();
    CHECK(sys.announcementsOf(c).empty());
}

// ---------------- 重做（双栈）----------------
static void testRedo() {
    std::cout << "[测试] 重做（双栈）\n";
    ClubSystem sys;
    int a = sys.addMember("A", "2022");
    sys.undo();
    CHECK(sys.findMember(a) == nullptr);
    CHECK(sys.canRedo());
    sys.redo();
    CHECK(sys.findMember(a) != nullptr);
    // 新命令清空 redo 栈
    sys.addMember("B", "2022");
    sys.undo();                       // 撤销添加 B → redo 栈 = [add-B]
    CHECK(sys.canRedo());
    sys.addMember("C", "2022");       // 新命令清空 redo 栈
    CHECK(sys.canRedo() == false);
}

// ---------------- 好友驱动活动推荐 ----------------
static void testFriendRecActivities() {
    std::cout << "[测试] 好友驱动活动推荐\n";
    ClubSystem sys;
    int a = sys.addMember("A", "2022");
    int b = sys.addMember("B", "2022");
    int c = sys.addMember("C", "2022");
    int club = sys.createClub("社", "学术", "2020", "");
    int act = sys.addActivity(club, "赛事", "周六", 10);
    sys.joinClub(a, club);
    sys.joinClub(b, club);
    sys.joinClub(c, club);
    sys.signup(b, act);
    sys.signup(c, act);
    sys.addFriend(a, b);
    sys.addFriend(a, c);
    auto recs = sys.recommendActivities(a);
    CHECK(recs.size() == 1);
    CHECK(recs[0].first == act);
    CHECK(recs[0].second == 2);
}

// ---------------- 批量导入 ----------------
static void testImport() {
    std::cout << "[测试] 批量导入 CSV\n";
    ClubSystem sys;
    std::string csv = "张三,2022,Python;算法\n李四,2021,C++\n王五,2023,Java\n张三,2022,重复\n坏行\n";
    auto r = sys.importMembers(csv);
    CHECK(r.added == 3);
    CHECK(r.skippedDup == 1);
    CHECK(r.skippedInvalid == 1);
    CHECK(sys.findMemberByName("张三") >= 0);
    // 张三 的标签也应导入
    auto t = sys.membersWithTag("Python");
    CHECK(std::find(t.begin(), t.end(), sys.findMemberByName("张三")) != t.end());
    // 批量导入可一次性撤销
    sys.undo();
    CHECK(sys.findMemberByName("张三") == -1);
    CHECK(sys.findMemberByName("李四") == -1);
    CHECK(sys.findMemberByName("王五") == -1);
}

#include "regression.h"

int main() {
    testRegressions();
    testHashMap();
    testAVL();
    testHeap();
    testContainers();
    testFriendGraph();
    testSystemMembers();
    testSystemClubsActivities();
    testSystemApplications();
    testUndo();
    testRedo();
    testTagIndex();
    testOrgTree();
    testTimeConflict();
    testCapacityAdjust();
    testPriorityApp();
    testFriendPath();
    testClubLeaderboard();
    testAnnounce();
    testFriendRecActivities();
    testImport();
    testPersistence();

    std::cout << "\n==================================\n";
    if (g_fail == 0) {
        std::cout << "ALL TESTS PASSED (" << g_checks << " checks)\n";
        return 0;
    } else {
        std::cout << g_fail << " / " << g_checks << " checks FAILED\n";
        return 1;
    }
}
