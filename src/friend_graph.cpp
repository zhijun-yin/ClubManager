#include "friend_graph.h"

#include <queue>
#include <algorithm>

void FriendGraph::addFriend(int a, int b) {
    if (a == b) return;
    auto& va = adj_[a];
    auto& vb = adj_[b];
    if (std::find(va.begin(), va.end(), b) == va.end()) {
        va.push_back(b);
    }
    if (std::find(vb.begin(), vb.end(), a) == vb.end()) {
        vb.push_back(a);
    }
}

bool FriendGraph::removeFriend(int a, int b) {
    auto ita = adj_.find(a);
    auto itb = adj_.find(b);
    if (ita == adj_.end() || itb == adj_.end()) return false;
    auto& va = ita->second;
    auto& vb = itb->second;
    auto ra = std::find(va.begin(), va.end(), b);
    if (ra == va.end()) return false;
    va.erase(ra);
    auto rb = std::find(vb.begin(), vb.end(), a);
    vb.erase(rb);
    return true;
}

bool FriendGraph::areFriends(int a, int b) const {
    auto it = adj_.find(a);
    if (it == adj_.end()) return false;
    const auto& va = it->second;
    return std::find(va.begin(), va.end(), b) != va.end();
}

const std::vector<int>& FriendGraph::friendsOf(int id) const {
    static const std::vector<int> empty;
    auto it = adj_.find(id);
    if (it == adj_.end()) return empty;
    return it->second;
}

std::vector<FriendGraph::Recommendation> FriendGraph::recommend(int id) const {
    std::unordered_set<int> direct;
    auto it = adj_.find(id);
    if (it != adj_.end()) {
        for (int f : it->second) direct.insert(f);
    }

    // 统计二度人脉的共同好友数
    std::unordered_map<int, int> mutual;
    if (it != adj_.end()) {
        for (int f : it->second) {
            auto fit = adj_.find(f);
            if (fit == adj_.end()) continue;
            for (int ff : fit->second) {
                if (ff == id) continue;
                if (direct.count(ff)) continue;  // 已是直接好友
                ++mutual[ff];
            }
        }
    }

    std::vector<Recommendation> recs;
    recs.reserve(mutual.size());
    for (const auto& kv : mutual) {
        recs.push_back({kv.first, kv.second});
    }
    std::sort(recs.begin(), recs.end(),
              [](const Recommendation& a, const Recommendation& b) {
                  if (a.mutualFriends != b.mutualFriends)
                      return a.mutualFriends > b.mutualFriends;
                  return a.memberId < b.memberId;
              });
    return recs;
}

int FriendGraph::friendDistance(int from, int to) const {
    if (from == to) return 0;
    std::unordered_set<int> visited;
    std::queue<int> q;
    q.push(from);
    visited.insert(from);
    int dist = 0;
    while (!q.empty()) {
        ++dist;
        int level = static_cast<int>(q.size());
        for (int i = 0; i < level; ++i) {
            int u = q.front();
            q.pop();
            auto it = adj_.find(u);
            if (it == adj_.end()) continue;
            for (int v : it->second) {
                if (v == to) return dist;
                if (visited.insert(v).second) {
                    q.push(v);
                }
            }
        }
    }
    return -1;  // 不连通
}

size_t FriendGraph::edgeCount() const {
    size_t e = 0;
    for (const auto& kv : adj_) e += kv.second.size();
    return e / 2;
}

std::vector<std::pair<int, int>> FriendGraph::edges() const {
    std::vector<std::pair<int, int>> out;
    for (const auto& kv : adj_) {
        int a = kv.first;
        for (int b : kv.second) {
            if (a < b) out.emplace_back(a, b);  // 每对只输出一次
        }
    }
    return out;
}

std::vector<int> FriendGraph::path(int from, int to) const {
    std::vector<int> empty;
    if (from == to) return {from};
    std::unordered_map<int, int> parent;  // 节点 → 前驱
    std::unordered_set<int> visited;
    std::queue<int> q;
    q.push(from);
    visited.insert(from);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        auto it = adj_.find(u);
        if (it == adj_.end()) continue;
        for (int v : it->second) {
            if (!visited.insert(v).second) continue;
            parent[v] = u;
            if (v == to) {
                // 回溯还原路径
                std::vector<int> rev{to};
                int cur = to;
                while (cur != from) { cur = parent[cur]; rev.push_back(cur); }
                std::reverse(rev.begin(), rev.end());
                return rev;
            }
            q.push(v);
        }
    }
    return empty;  // 不连通
}
