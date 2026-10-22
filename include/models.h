#ifndef MODELS_H
#define MODELS_H

#include "heap.h"

#include <string>
#include <vector>
#include <utility>

// 成员（用户账号）。id 即其在系统 vectors 中的下标，便于 O(1) 访问。
struct Member {
    int id = -1;
    std::string name;
    std::string grade;     // 年级，如 "2023"
    int score = 0;         // 活跃度积分
    long long joinSeq = 0; // 加入序号（用于确定性排序）
    std::vector<std::string> tags;  // 技能/兴趣标签（用于倒排索引）
    bool active = true;    // 逻辑删除标记
    std::string accountKey; // 可选的稳定账号绑定标识，不随普通 ID 重用
};

// 社团
struct Club {
    int id = -1;
    std::string name;
    std::string category;   // 分类，如 "学术科技"
    std::string founded;    // 成立日期字符串
    std::string description;
    std::vector<int> memberIds;       // 已入社成员 id（经申请审批通过）
    std::vector<int> subClubs;        // 子部门（社团层级树中的子节点 id）
    int parentClub = -1;              // 父社团 id，-1 表示顶层
    bool active = true;
};

// 活动
struct Activity {
    int id = -1;
    int clubId = -1;
    std::string name;
    std::string time;       // 时间字符串（展示用）
    int capacity = 0;
    std::vector<int> signedUp;                        // 已报名成员 id（按报名先后）
    BinaryHeap<std::pair<int, int>> waitlist;         // 候补名单 (活跃度, 成员id) 最大堆
    std::vector<std::string> requiredTags;            // 参与所需的标签
    // 结构化时间段（用于冲突检测）；day: 0=周日..6=周六；-1 表示未提供
    int day = -1;
    int startMin = -1;   // 自 00:00 起的分钟数
    int endMin = -1;
    bool active = true;

    std::vector<int> checkedIn;  // 已签到者；每人每活动奖励 10 分
    bool closed = false;        // 已结束活动保留历史，不再占用日程

    bool timed() const { return day >= 0 && startMin >= 0 && endMin >= 0; }
};

// 入社申请。status: 0 待审, 1 通过, -1 拒绝, -2 撤回
struct Application {
    int id = -1;
    int memberId = -1;
    int clubId = -1;
    long long submitSeq = 0;
    int priority = 0;   // 优先级：0 普通，1 社团骨干，2 加急（越大越优先）
    int status = 0;
};

// 社团公告
struct Announcement {
    int id = -1;
    int clubId = -1;
    int senderId = -1;
    std::string message;
    long long seq = 0;
};

#endif // MODELS_H
