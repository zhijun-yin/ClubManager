#include "data_io.h"
#include <fstream>
#include <limits>
#include <random>

static void testRegressions() {
    std::cout << "[测试] 完整撤销、候补资格、签到、损坏存档、CSV 与随机 AVL 对照\n";
    Queue<int> zeroQueue(0);
    CHECK(zeroQueue.push(42)); CHECK(zeroQueue.front() == 42); CHECK(zeroQueue.pop());
    ClubSystem s;
    CHECK(s.addMember(" \t", "") == -1);
    int a = s.addMember("A|%;\n测试", "2026", {"C++", "C++", "x;y", ""});
    int b = s.addMember("B", "2026", {"C++"});
    int c = s.addMember("C", "2026", {"C++"});
    int club = s.createClub("Club|X", "", "", "line1\nline2");
    CHECK(s.findMember(a)->tags.size() == 2);
    CHECK(s.joinClub(club, a));
    CHECK(!s.removeMember(a));
    CHECK(s.undo()); CHECK(s.findClub(club)->memberIds.empty());
    CHECK(s.redo()); CHECK(s.leaveClub(club, a));
    CHECK(s.undo()); CHECK(s.findClub(club)->memberIds == std::vector<int>{a});
    CHECK(s.redo());
    CHECK(s.addActivity(club, "invalid", "", -1) == -1);
    CHECK(s.addActivity(club, "invalid", "", 1, {}, 7, 100, 200) == -1);
    CHECK(s.addActivity(club, "invalid", "", 1, {}, 1, 200, 100) == -1);
    CHECK(s.addActivity(club, "invalid", "", 1, {}, 1, 100, 1441) == -1);
    int act = s.addActivity(club, "A|B", "", 1, {"C++"}, 1, 100, 200);
    int clash = s.addActivity(club, "clash", "", 1, {}, 1, 150, 220);
    CHECK(!s.removeClub(club));
    CHECK(s.addActivity(club, "A|B", "", 2) == -1);
    CHECK(s.signup(a, act).find("成功") != std::string::npos);
    s.signup(b, act); s.signup(c, act);
    CHECK(s.undo()); CHECK(s.activityWaitlistIds(act) == std::vector<int>{b});
    CHECK(s.redo()); CHECK(s.activityWaitlistIds(act).size() == 2);
    CHECK(s.cancelSignup(a, act));
    CHECK(s.findActivity(act)->signedUp == std::vector<int>{b});
    CHECK(s.undo()); CHECK(s.findActivity(act)->signedUp == std::vector<int>{a});
    CHECK(s.activityWaitlistIds(act).size() == 2);
    CHECK(s.redo()); CHECK(s.findActivity(act)->signedUp == std::vector<int>{b});
    CHECK(s.undo());
    s.addScore(b, 30); // 候补报名后积分变化
    CHECK(s.signup(b, clash).find("成功") != std::string::npos);
    CHECK(s.cancelSignup(a, act)); // b 冲突，必须越过 b 提拔 c
    CHECK(s.findActivity(act)->signedUp == std::vector<int>{c});
    CHECK(s.activityWaitlistIds(act) == std::vector<int>{b});
    CHECK(s.setActivityCapacity(act, 3)); // 唯一候补冲突，不能死循环或强行转正
    CHECK(s.findActivity(act)->signedUp.size() == 1);
    CHECK(s.removeMemberTag(a, "C++"));
    CHECK(s.signup(a, act).find("标签") != std::string::npos);
    CHECK(!s.checkIn(b, act));
    CHECK(s.checkIn(c, act));
    CHECK(s.findMember(c)->score == 10);
    CHECK(!s.checkIn(c, act)); CHECK(!s.cancelSignup(c, act)); CHECK(!s.setActivityCapacity(act, 0));
    CHECK(s.undo()); CHECK(s.findMember(c)->score == 0); CHECK(s.findActivity(act)->checkedIn.empty());
    CHECK(s.redo()); CHECK(s.findMember(c)->score == 10);
    CHECK(s.memberSchedule(c) == std::vector<int>{act});
    CHECK(s.closeActivity(act));
    CHECK(!s.checkIn(a, act)); CHECK(!s.cancelSignup(c, act)); CHECK(!s.setActivityCapacity(act, 4));
    CHECK(s.signup(a, act).find("结束") != std::string::npos);
    CHECK(s.undo()); CHECK(!s.findActivity(act)->closed); CHECK(s.redo());
    auto stats = s.statistics();
    CHECK(stats.members == 3 && stats.clubs == 1 && stats.activities == 2);
    CHECK(stats.closedActivities == 1 && stats.checkIns == 1 && stats.totalScore == 40);
    CHECK(s.exportRosterCsv(act).find("已签到") != std::string::npos);
    CHECK(s.exportRosterCsv(99).empty());
    CHECK(s.broadcast(club, a, "hello|world\n100%;") >= 0);
    CHECK(s.save("_regression.txt"));
    ClubSystem loaded;
    CHECK(loaded.load("_regression.txt"));
    CHECK(loaded.findMember(a)->name == s.findMember(a)->name);
    CHECK(loaded.findMember(a)->tags == s.findMember(a)->tags);
    CHECK(loaded.findActivity(act)->closed && loaded.findActivity(act)->checkedIn == std::vector<int>{c});
    CHECK(loaded.announcementsOf(club)[0].message == "hello|world\n100%;");
    CHECK(loaded.statistics().totalScore == 40);
    int removed = loaded.addMember("removed", ""); CHECK(loaded.removeMember(removed));
    CHECK(loaded.save("_regression.txt")); CHECK(loaded.load("_regression.txt"));
    CHECK(loaded.findMemberByName("removed") == -1 && loaded.memberRank(removed) == -1);
    CHECK(loaded.leaderboard(99).size() == 3);
    loaded.addScore(a, 1);
    std::ifstream file("_regression.txt"); std::string good((std::istreambuf_iterator<char>(file)), {}); file.close();
    std::vector<std::string> bad = {"", "garbage", "# ClubManager save v2\nclock|x\n", good.substr(0, good.size()-5)};
    auto invalidId = good; invalidId.replace(invalidId.find("M|0|"), 4, "M|-1|"); bad.push_back(invalidId);
    auto invalidEscape = good; invalidEscape.replace(invalidEscape.find("%7C"), 3, "%XX"); bad.push_back(invalidEscape);
    auto invalidRef = good; invalidRef.replace(invalidRef.find("A|0|0|"), 6, "A|0|999|"); bad.push_back(invalidRef);
    auto invalidCycle = good; auto pos = invalidCycle.find("|-1|", invalidCycle.find("C|0|")); invalidCycle.replace(pos, 4, "|0|"); bad.push_back(invalidCycle);
    for (const auto& data : bad) {
        std::ofstream out("_bad.txt"); out << data; out.close();
        CHECK(!loaded.load("_bad.txt")); CHECK(loaded.statistics().members == 3); CHECK(loaded.canUndo());
    }
    CHECK(loaded.undo()); CHECK(loaded.findMember(a)->score == 0);
    CHECK(!loaded.save("_missing_directory/out.txt"));
    std::remove("_bad.txt"); std::remove("_regression.txt");

    ClubSystem apps;
    int m = apps.addMember("member", ""), n = apps.addMember("other", "");
    int cl = apps.createClub("club", "", "", "");
    int p = apps.submitApplication(m, cl, 3);
    CHECK(apps.submitApplication(m, cl) == -1);
    CHECK(apps.processNextApplication() == p && apps.pendingCount() == 1);
    CHECK(!apps.withdrawApplication(p, n)); CHECK(apps.withdrawApplication(p, m));
    CHECK(apps.pendingCount() == 0); CHECK(apps.undo()); CHECK(apps.pendingCount() == 1);
    CHECK(apps.reviewApplication(p, true)); CHECK(apps.pendingCount() == 0);
    CHECK(apps.undo()); CHECK(apps.pendingCount() == 1 && apps.findClub(cl)->memberIds.empty());
    CHECK(apps.redo()); CHECK(apps.pendingCount() == 0 && apps.findClub(cl)->memberIds == std::vector<int>{m});
    apps.addFriend(m,n); CHECK(apps.removeMember(n)); CHECK(apps.friendDistance(m,n)==-1);
    CHECK(apps.undo()); CHECK(apps.friendDistance(m,n)==1);
    apps.addScore(m, std::numeric_limits<int>::max()); apps.addScore(m, 10);
    CHECK(apps.findMember(m)->score == std::numeric_limits<int>::max());
    apps.addScore(m, std::numeric_limits<int>::min()); CHECK(apps.findMember(m)->score == 0);

    ClubSystem csv;
    auto imported=csv.importMembers("\xEF\xBB\xBF姓名,年级,标签\r\n\"Doe, Jane\",2026,\"C++;算法\"\r\n\"bad,2026\r\nJane,2026,\r\nJane,2026,\r\n");
    CHECK(imported.added == 2 && imported.skippedDup == 1 && imported.skippedInvalid == 1);
    CHECK(csv.findMemberByName("Doe, Jane") == 0);
    CHECK(csv.exportMembersCsv().find("\"Doe, Jane\"") != std::string::npos);
    CHECK(club_io::csvCell("=1+1") == "\"'=1+1\"");
    CHECK(csv.undo()); CHECK(csv.statistics().members == 0); CHECK(csv.redo());

    AVL tree;
    std::vector<std::pair<int,int>> reference;
    std::mt19937 random(20261022);
    for (int i=0; i<200; ++i) { int score=static_cast<int>(random()%50); tree.insert(score,i); reference.push_back({score,i}); }
    for (int i=0; i<100; ++i) {
        int lo=static_cast<int>(random()%70)-10, hi=static_cast<int>(random()%70)-10;
        int expected=0;
        for (const auto& p2 : reference) if (p2.first>=lo && p2.first<=hi) ++expected;
        CHECK(tree.rangeCount(lo,hi)==expected);
    }
    std::sort(reference.begin(), reference.end(), std::greater<std::pair<int,int>>());
    CHECK(tree.topK(200)==reference);
    for (size_t i=0; i<reference.size(); ++i) CHECK(tree.rankOf(reference[i].first,reference[i].second)==static_cast<int>(i));
    for (size_t i=0; i<reference.size(); i+=2) tree.remove(reference[i].first,reference[i].second);
    std::vector<std::pair<int,int>> remaining;
    for (size_t i=1; i<reference.size(); i+=2) remaining.push_back(reference[i]);
    CHECK(tree.topK(200)==remaining);
}
