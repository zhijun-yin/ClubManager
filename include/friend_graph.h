#ifndef FRIEND_GRAPH_H
#define FRIEND_GRAPH_H

#include <vector>
#include <unordered_set>
#include <unordered_map>

// 成员好友关系图（无向，邻接表）。用于二度人脉推荐与最短关系距离（BFS）。
class FriendGraph {
public:
    // 添加一条好友关系（无向）
    void addFriend(int a, int b);

    // 删除好友关系，返回是否成功
    bool removeFriend(int a, int b);

    bool areFriends(int a, int b) const;

    // 某成员的好友 id 列表
    const std::vector<int>& friendsOf(int id) const;

    // 二度人脉推荐：好友的好友中，尚未直接好友的人，按共同好友数降序（再按 id 升序）。
    struct Recommendation {
        int memberId;
        int mutualFriends;  // 共同好友数量
    };
    std::vector<Recommendation> recommend(int id) const;

    // BFS 最短关系距离（跳数）；不连通返回 -1。
    int friendDistance(int from, int to) const;

    // BFS 最短路径（含端点，顺序 from→to）；不连通返回空向量。
    std::vector<int> path(int from, int to) const;

    size_t memberCount() const { return adj_.size(); }
    // 好友关系（无向边）总数
    size_t edgeCount() const;

    // 清空整张图
    void clear() { adj_.clear(); }

    // 返回全部无向边（每对只出现一次），供持久化
    std::vector<std::pair<int, int>> edges() const;

private:
    std::unordered_map<int, std::vector<int>> adj_;
};

#endif // FRIEND_GRAPH_H
