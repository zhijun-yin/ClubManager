#ifndef SYSTEM_H
#define SYSTEM_H

#include "models.h"
#include "hash_map.h"
#include "avl.h"
#include "heap.h"
#include "friend_graph.h"
#include "inverted_index.h"
#include "containers.h"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// 待审申请在优先队列中的元素。最大堆按 (优先级, 提交先后) 排序：
// 优先级高者优先；同优先级先提交者优先（seq 小者“更大”）。
struct PendingApp {
    int priority;
    long long seq;
    int appId;
};
inline bool operator<(const PendingApp& a, const PendingApp& b) {
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.seq > b.seq;
}

// 社团管理系统核心。整合哈希表、AVL、堆、栈、队列、图、倒排索引等数据结构。
class ClubSystem {
public:
    ClubSystem();
    ClubSystem(const ClubSystem&) = delete;
    ClubSystem& operator=(const ClubSystem&) = delete;

    // ---- 成员 ----
    int addMember(const std::string& name, const std::string& grade);
    int addMember(const std::string& name, const std::string& grade,
                  const std::vector<std::string>& tags);
    bool removeMember(int id);   // 有报名/候补/待审申请则拒绝
    void addScore(int id, int delta);
    bool addMemberTag(int id, const std::string& tag);
    bool removeMemberTag(int id, const std::string& tag);
    Member* findMember(int id);
    const Member* findMember(int id) const;
    int findMemberByName(const std::string& name) const;
    const std::vector<Member>& members() const { return members_; }

    // 标签 / 倒排索引查询
    std::vector<int> membersWithTag(const std::string& tag) const;
    std::vector<int> membersWithAllTags(const std::vector<std::string>& tags) const;
    std::vector<int> membersWithAnyTag(const std::vector<std::string>& tags) const;
    std::vector<std::string> allTags() const;

    // ---- 社团 ----
    int createClub(const std::string& name, const std::string& category,
                   const std::string& founded, const std::string& description);
    // 在父社团下创建子部门（社团层级树），返回子社团 id；父不存在返回 -1
    int createSubClub(int parentId, const std::string& name,
                      const std::string& category, const std::string& founded,
                      const std::string& description);
    bool removeClub(int id);   // 有在册成员或子部门则拒绝
    Club* findClub(int id);
    int findClubByName(const std::string& name) const;
    bool joinClub(int clubId, int memberId);
    bool leaveClub(int clubId, int memberId);
    const std::vector<Club>& clubs() const { return clubs_; }
    // 社团组织架构（DFS 前序遍历），每行缩进表示深度
    std::vector<std::string> orgTreeLines() const;

    // ---- 活动 ----
    int addActivity(int clubId, const std::string& name,
                    const std::string& time, int capacity);
    // 扩展版：可带所需标签与结构化时间段
    int addActivity(int clubId, const std::string& name, const std::string& time,
                    int capacity, const std::vector<std::string>& requiredTags,
                    int day, int startMin, int endMin);
    bool removeActivity(int id);
    // 调整容量：缩容时多余正取名额按活跃度降级进候补，扩容时从候补提拔
    bool setActivityCapacity(int activityId, int newCapacity);
    std::string signup(int memberId, int activityId);   // 满员进候补；时间冲突则拒绝
    bool cancelSignup(int memberId, int activityId);     // 自动提拔候补
    Activity* findActivity(int id);
    int findActivityByName(const std::string& name) const;
    const std::vector<Activity>& activities() const { return activities_; }
    bool checkIn(int memberId, int activityId);
    bool closeActivity(int activityId);
    std::vector<int> memberSchedule(int memberId) const;
    std::string exportMembersCsv() const;
    std::string exportRosterCsv(int activityId) const;
    struct Statistics {
        int members = 0, clubs = 0, activities = 0, closedActivities = 0;
        int registrations = 0, waiting = 0, checkIns = 0, pending = 0;
        long long totalScore = 0;
    };
    Statistics statistics() const;

    // ---- 申请审批（优先级队列）----
    int submitApplication(int memberId, int clubId, int priority = 0);
    int processNextApplication();                          // 查看优先级最高者，审批/撤回才出队
    bool reviewApplication(int appId, bool approve);
    bool withdrawApplication(int appId, int memberId);
    const std::vector<Application>& applications() const { return applications_; }
    int pendingCount() const;

    // ---- 好友关系（图 + BFS）----
    void addFriend(int a, int b);
    bool removeFriend(int a, int b);
    std::vector<FriendGraph::Recommendation> recommendFriends(int id) const;
    int friendDistance(int a, int b) const;
    std::vector<int> friendPath(int a, int b) const;      // 最短社交路径（含端点）

    // ---- 好友驱动的活动推荐 ----
    std::vector<std::pair<int, int>> recommendActivities(int id) const;  // (actId, 分数)

    // ---- 排行榜 / 统计（AVL）----
    std::vector<std::pair<int, int>> leaderboard(size_t k) const;  // (score,id) 降序
    int memberRank(int id) const;
    int rangeActive(int lo, int hi) const;
    // 社团活跃度排行：按在册成员活跃度总和降序 (总分, clubId)
    std::vector<std::pair<long long, int>> clubLeaderboard(size_t k) const;

    // ---- 公告广播 ----
    int broadcast(int clubId, int senderId, const std::string& message);
    std::vector<Announcement> announcementsOf(int clubId) const;
    // 某成员能收到的公告（其所在社团的公告）
    std::vector<Announcement> announcementsForMember(int memberId) const;

    // ---- 撤销 / 重做（双栈 + 命令模式）----
    bool undo();
    bool redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // ---- 批量导入 ----
    struct ImportResult {
        int added = 0;
        int skippedDup = 0;     // 重名跳过
        int skippedInvalid = 0; // 行格式错误跳过
    };
    // 每行格式： 姓名,年级,标签1;标签2;标签3
    ImportResult importMembers(const std::string& csv);

    // ---- 持久化 ----
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // 候补名单持久化辅助
    std::vector<int> activityWaitlistIds(int activityId) const;
    void setActivityWaitlist(int activityId, const std::vector<int>& ids);

private:
    long long nextSeq() { return ++clock_; }

    std::vector<Member> members_;
    std::vector<Club> clubs_;
    std::vector<Activity> activities_;
    std::vector<Application> applications_;
    std::vector<Announcement> announcements_;

    StringIntMap memberByName_;
    StringIntMap clubByName_;
    StringIntMap activityByName_;

    AVL scoreIndex_;            // (score, id) 有序索引
    FriendGraph friendGraph_;
    InvertedIndex tagIndex_;    // 标签 → 成员 id 倒排索引
    BinaryHeap<PendingApp> pendingApps_;  // 待审申请优先队列（堆）

    long long clock_ = 0;
    int nextMemberId_ = 0;
    int nextClubId_ = 0;
    int nextActivityId_ = 0;
    int nextAppId_ = 0;
    int nextAnnouncementId_ = 0;

    // 撤销/重做双栈：每个命令同时记录逆操作与顺操作
    struct Cmd {
        std::function<void()> undo;
        std::function<void()> redo;
    };
    std::vector<Cmd> undoStack_;
    std::vector<Cmd> redoStack_;
    void pushCmd(std::function<void()> u, std::function<void()> r) {
        undoStack_.push_back({std::move(u), std::move(r)});
        redoStack_.clear();
    }

    // 供撤销/重做使用的内部原语（不二次记录）
    void _removeMemberInternal(int id);
    void _addMemberInternal(const Member& m);
    void _setScoreInternal(int id, int score);
    void _removeClubInternal(int id);
    void _addClubInternal(const Club& c);
    void _removeActivityInternal(int id);
    void _addActivityInternal(const Activity& a);
    void _addTagToIndex(const Member& m);
    void _removeTagFromIndex(int id);
    void _addTagInternal(int id, const std::string& tag);
    void _removeTagInternal(int id, const std::string& tag);
    int _promoteFromWaitlist(int activityId);
    bool _cancelSignupInternal(int memberId, int activityId);
    void _rebuildPendingQueue();
    void _restoreSignup(int activityId, const std::vector<int>& signedUp,
                        const std::vector<int>& waitlistIds, int capacity);
    bool _hasTimeConflict(int memberId, int activityId) const;
    bool _hasRequiredTags(int memberId, const Activity& activity) const;
    void _dfsOrg(int clubId, int depth, std::vector<std::string>& out) const;
};

#endif // SYSTEM_H
