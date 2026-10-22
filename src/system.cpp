#include "system.h"
#include "data_io.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cctype>
#include <limits>

namespace {

bool containsId(const std::vector<int>& v, int id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitStr(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string cell;
    while (std::getline(ss, cell, delim)) out.push_back(cell);
    return out;
}

std::vector<std::string> parseStrs(const std::string& s) {
    std::vector<std::string> out;
    for (auto& t : splitStr(s, ';')) { t = trim(t); if (!t.empty()) out.push_back(t); }
    return out;
}

} // namespace

ClubSystem::ClubSystem() = default;

// ==================== 成员 ====================

int ClubSystem::addMember(const std::string& name, const std::string& grade) {
    return addMember(name, grade, {});
}

int ClubSystem::addMember(const std::string& name, const std::string& grade,
                          const std::vector<std::string>& tags) {
    if (trim(name).empty() || memberByName_.contains(name)) return -1;
    Member m;
    m.id = nextMemberId_++;
    m.name = name;
    m.grade = grade;
    m.score = 0;
    m.joinSeq = nextSeq();
    for (const auto& tag : tags)
        if (!trim(tag).empty() && std::find(m.tags.begin(), m.tags.end(), tag) == m.tags.end()) m.tags.push_back(tag);
    _addMemberInternal(m);

    Member snapshot = m;
    int id = m.id;
    pushCmd([this, id]() { _removeMemberInternal(id); },
            [this, snapshot]() { _addMemberInternal(snapshot); });
    return id;
}

void ClubSystem::_addMemberInternal(const Member& m) {
    if (m.id >= static_cast<int>(members_.size())) members_.resize(m.id + 1);
    auto binding = members_[m.id].accountKey;
    members_[m.id] = m;
    if (members_[m.id].accountKey.empty()) members_[m.id].accountKey = binding;
    memberByName_.put(m.name, m.id);
    scoreIndex_.insert(m.score, m.id);
    _addTagToIndex(m);
}

void ClubSystem::_removeMemberInternal(int id) {
    if (id < 0 || id >= static_cast<int>(members_.size())) return;
    Member& m = members_[id];
    if (!m.active) return;
    scoreIndex_.remove(m.score, id);
    memberByName_.remove(m.name);
    _removeTagFromIndex(id);
    m.active = false;
}

bool ClubSystem::removeMember(int id) {
    Member* m = findMember(id);
    if (!m) return false;
    for (const auto& act : activities_) {
        if (!act.active) continue;
        if (containsId(act.signedUp, id)) return false;
        for (auto& p : act.waitlist.snapshot()) if (p.second == id) return false;
    }
    for (const auto& app : applications_) {
        if (app.status == 0 && app.memberId == id) return false;
    }
    for (const auto& club : clubs_) if (club.active && containsId(club.memberIds, id)) return false;
    Member snapshot = *m;
    auto friends = friendGraph_.friendsOf(id);
    for (int other : friends) friendGraph_.removeFriend(id, other);
    _removeMemberInternal(id);
    pushCmd([this, snapshot, friends]() {
                _addMemberInternal(snapshot);
                for (int other : friends) friendGraph_.addFriend(snapshot.id, other);
            },
            [this, id, friends]() {
                for (int other : friends) friendGraph_.removeFriend(id, other);
                _removeMemberInternal(id);
            });
    return true;
}

void ClubSystem::addScore(int id, int delta) {
    Member* m = findMember(id);
    if (!m) return;
    int oldScore = m->score;
    int newScore = static_cast<int>(std::clamp(static_cast<long long>(oldScore) + delta,
                                               0LL, static_cast<long long>(std::numeric_limits<int>::max())));
    if (newScore == oldScore) return;
    _setScoreInternal(id, newScore);
    pushCmd([this, id, oldScore]() { _setScoreInternal(id, oldScore); },
            [this, id, newScore]() { _setScoreInternal(id, newScore); });
}

void ClubSystem::_setScoreInternal(int id, int score) {
    Member* m = findMember(id);
    if (!m) return;
    scoreIndex_.remove(m->score, id);
    m->score = score;
    scoreIndex_.insert(score, id);
}

bool ClubSystem::addMemberTag(int id, const std::string& tag) {
    Member* m = findMember(id);
    if (!m || trim(tag).empty()) return false;
    for (auto& t : m->tags) if (t == tag) return false;
    _addTagInternal(id, tag);
    pushCmd([this, id, tag]() { _removeTagInternal(id, tag); },
            [this, id, tag]() { _addTagInternal(id, tag); });
    return true;
}

bool ClubSystem::removeMemberTag(int id, const std::string& tag) {
    Member* m = findMember(id);
    if (!m) return false;
    bool has = false;
    for (auto& t : m->tags) if (t == tag) { has = true; break; }
    if (!has) return false;
    _removeTagInternal(id, tag);
    pushCmd([this, id, tag]() { _addTagInternal(id, tag); },
            [this, id, tag]() { _removeTagInternal(id, tag); });
    return true;
}

void ClubSystem::_addTagToIndex(const Member& m) {
    for (auto& t : m.tags) if (!t.empty()) tagIndex_.add(t, m.id);
}
void ClubSystem::_removeTagFromIndex(int id) { tagIndex_.removeDoc(id); }
void ClubSystem::_addTagInternal(int id, const std::string& tag) {
    Member* m = findMember(id);
    if (!m || tag.empty()) return;
    for (auto& t : m->tags) if (t == tag) return;
    m->tags.push_back(tag);
    tagIndex_.add(tag, id);
}
void ClubSystem::_removeTagInternal(int id, const std::string& tag) {
    Member* m = findMember(id);
    if (!m) return;
    auto& v = m->tags;
    v.erase(std::remove(v.begin(), v.end(), tag), v.end());
    tagIndex_.remove(tag, id);
}

Member* ClubSystem::findMember(int id) {
    if (id < 0 || id >= static_cast<int>(members_.size())) return nullptr;
    if (!members_[id].active) return nullptr;
    return &members_[id];
}
const Member* ClubSystem::findMember(int id) const {
    if (id < 0 || id >= static_cast<int>(members_.size())) return nullptr;
    if (!members_[id].active) return nullptr;
    return &members_[id];
}
int ClubSystem::findMemberByName(const std::string& name) const {
    int id = -1;
    if (!memberByName_.get(name, id)) return -1;
    return id;
}

std::vector<int> ClubSystem::membersWithTag(const std::string& tag) const {
    return tagIndex_.query(tag);
}
std::vector<int> ClubSystem::membersWithAllTags(const std::vector<std::string>& tags) const {
    return tagIndex_.queryAnd(tags);
}
std::vector<int> ClubSystem::membersWithAnyTag(const std::vector<std::string>& tags) const {
    return tagIndex_.queryOr(tags);
}
std::vector<std::string> ClubSystem::allTags() const {
    return tagIndex_.terms();
}

// ==================== 社团 ====================

int ClubSystem::createClub(const std::string& name, const std::string& category,
                           const std::string& founded, const std::string& description) {
    if (trim(name).empty() || clubByName_.contains(name)) return -1;
    Club c;
    c.id = nextClubId_++;
    c.name = name; c.category = category; c.founded = founded; c.description = description;
    _addClubInternal(c);
    Club snapshot = c;
    int id = c.id;
    pushCmd([this, id]() { _removeClubInternal(id); },
            [this, snapshot]() { _addClubInternal(snapshot); });
    return id;
}

int ClubSystem::createSubClub(int parentId, const std::string& name,
                              const std::string& category, const std::string& founded,
                              const std::string& description) {
    if (!findClub(parentId)) return -1;  // 父不存在
    if (trim(name).empty() || clubByName_.contains(name)) return -1;
    Club c;
    c.id = nextClubId_++;
    c.name = name; c.category = category; c.founded = founded; c.description = description;
    c.parentClub = parentId;
    _addClubInternal(c);
    // 重新获取父指针：_addClubInternal 可能触发 clubs_ resize 使旧指针失效
    if (Club* p = findClub(parentId)) p->subClubs.push_back(c.id);

    Club snapshot = c;
    int pid = parentId;
    pushCmd(
        [this, id = c.id, pid]() {  // undo：删除子部门
            _removeClubInternal(id);
            if (Club* pp = findClub(pid)) {
                auto& v = pp->subClubs;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
            }
        },
        [this, snapshot, pid]() {  // redo：重新创建子部门
            _addClubInternal(snapshot);
            if (Club* pp = findClub(pid)) {
                if (!containsId(pp->subClubs, snapshot.id)) pp->subClubs.push_back(snapshot.id);
            }
        });
    return c.id;
}

void ClubSystem::_addClubInternal(const Club& c) {
    if (c.id >= static_cast<int>(clubs_.size())) clubs_.resize(c.id + 1);
    clubs_[c.id] = c;
    clubByName_.put(c.name, c.id);
}
void ClubSystem::_removeClubInternal(int id) {
    if (id < 0 || id >= static_cast<int>(clubs_.size())) return;
    Club& c = clubs_[id];
    if (!c.active) return;
    clubByName_.remove(c.name);
    c.active = false;
}

bool ClubSystem::removeClub(int id) {
    Club* c = findClub(id);
    if (!c) return false;
    for (int mid : c->memberIds) if (findMember(mid)) return false;
    for (int sub : c->subClubs) if (findClub(sub)) return false;
    for (const auto& a : activities_) if (a.active && a.clubId == id) return false;
    for (const auto& a : applications_) if (a.status == 0 && a.clubId == id) return false;
    Club snapshot = *c;
    int pid = c->parentClub;
    _removeClubInternal(id);
    if (Club* p = findClub(pid)) {
        auto& v = p->subClubs;
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
    }
    pushCmd(
        [this, snapshot, pid]() {
            _addClubInternal(snapshot);
            if (Club* p = findClub(pid)) {
                if (!containsId(p->subClubs, snapshot.id)) p->subClubs.push_back(snapshot.id);
            }
        },
        [this, id, pid]() {
            _removeClubInternal(id);
            if (Club* p = findClub(pid)) {
                auto& v = p->subClubs;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
            }
        });
    return true;
}

Club* ClubSystem::findClub(int id) {
    if (id < 0 || id >= static_cast<int>(clubs_.size())) return nullptr;
    if (!clubs_[id].active) return nullptr;
    return &clubs_[id];
}
int ClubSystem::findClubByName(const std::string& name) const {
    int id = -1;
    if (!clubByName_.get(name, id)) return -1;
    return id;
}
bool ClubSystem::joinClub(int clubId, int memberId) {
    Club* c = findClub(clubId);
    Member* m = findMember(memberId);
    if (!c || !m) return false;
    if (containsId(c->memberIds, memberId)) return false;
    auto before = c->memberIds;
    c->memberIds.push_back(memberId);
    auto after = c->memberIds;
    pushCmd([this, clubId, before]() { clubs_[clubId].memberIds = before; },
            [this, clubId, after]() { clubs_[clubId].memberIds = after; });
    return true;
}
bool ClubSystem::leaveClub(int clubId, int memberId) {
    Club* c = findClub(clubId);
    if (!c) return false;
    auto& v = c->memberIds;
    auto it = std::find(v.begin(), v.end(), memberId);
    if (it == v.end()) return false;
    auto before = v;
    v.erase(it);
    auto after = v;
    pushCmd([this, clubId, before]() { clubs_[clubId].memberIds = before; },
            [this, clubId, after]() { clubs_[clubId].memberIds = after; });
    return true;
}

void ClubSystem::_dfsOrg(int clubId, int depth, std::vector<std::string>& out) const {
    const Club* c = (clubId >= 0 && clubId < static_cast<int>(clubs_.size())) ? &clubs_[clubId] : nullptr;
    if (!c || !c->active) return;
    out.push_back(std::string(depth * 2, ' ') + "└ " + c->name + " (#" + std::to_string(c->id) + ")");
    for (int sub : c->subClubs) _dfsOrg(sub, depth + 1, out);
}
std::vector<std::string> ClubSystem::orgTreeLines() const {
    std::vector<std::string> out;
    // 从所有顶层社团（parentClub == -1）开始 DFS
    for (const auto& c : clubs_) {
        if (c.active && c.parentClub == -1) _dfsOrg(c.id, 0, out);
    }
    return out;
}

// ==================== 活动 ====================

int ClubSystem::addActivity(int clubId, const std::string& name,
                            const std::string& time, int capacity) {
    return addActivity(clubId, name, time, capacity, {}, -1, -1, -1);
}

int ClubSystem::addActivity(int clubId, const std::string& name, const std::string& time,
                            int capacity, const std::vector<std::string>& requiredTags,
                            int day, int startMin, int endMin) {
    Club* c = findClub(clubId);
    if (!c || trim(name).empty() || activityByName_.contains(name) || capacity < 0) return -1;
    bool untimed = day == -1 && startMin == -1 && endMin == -1;
    if (!untimed && (day < 0 || day > 6 || startMin < 0 || startMin >= endMin || endMin > 1440)) return -1;
    Activity a;
    a.id = nextActivityId_++;
    a.clubId = clubId; a.name = name; a.time = time; a.capacity = capacity;
    a.requiredTags = requiredTags; a.day = day; a.startMin = startMin; a.endMin = endMin;
    _addActivityInternal(a);
    Activity snapshot = a;
    int id = a.id;
    pushCmd([this, id]() { _removeActivityInternal(id); },
            [this, snapshot]() { _addActivityInternal(snapshot); });
    return id;
}

void ClubSystem::_addActivityInternal(const Activity& a) {
    if (a.id >= static_cast<int>(activities_.size())) activities_.resize(a.id + 1);
    activities_[a.id] = a;
    activityByName_.put(a.name, a.id);
}
void ClubSystem::_removeActivityInternal(int id) {
    if (id < 0 || id >= static_cast<int>(activities_.size())) return;
    Activity& a = activities_[id];
    if (!a.active) return;
    activityByName_.remove(a.name);
    a.active = false;
}
bool ClubSystem::removeActivity(int id) {
    Activity* a = findActivity(id);
    if (!a) return false;
    Activity snapshot = *a;
    _removeActivityInternal(id);
    pushCmd([this, snapshot]() { _addActivityInternal(snapshot); },
            [this, id]() { _removeActivityInternal(id); });
    return true;
}

Activity* ClubSystem::findActivity(int id) {
    if (id < 0 || id >= static_cast<int>(activities_.size())) return nullptr;
    if (!activities_[id].active) return nullptr;
    return &activities_[id];
}
int ClubSystem::findActivityByName(const std::string& name) const {
    int id = -1;
    if (!activityByName_.get(name, id)) return -1;
    return id;
}

void ClubSystem::_restoreSignup(int activityId, const std::vector<int>& signedUp,
                                const std::vector<int>& waitlistIds, int capacity) {
    Activity* a = findActivity(activityId);
    if (!a) return;
    a->signedUp = signedUp;
    a->capacity = capacity;
    a->waitlist.clear();
    for (int mid : waitlistIds) {
        if (const Member* m = findMember(mid)) a->waitlist.push({m->score, mid});
    }
}

bool ClubSystem::setActivityCapacity(int activityId, int newCapacity) {
    Activity* a = findActivity(activityId);
    if (!a || a->closed || newCapacity < 0 || newCapacity < static_cast<int>(a->checkedIn.size())) return false;
    if (newCapacity == a->capacity) return false;

    std::vector<int> oldSigned = a->signedUp;
    std::vector<int> oldWait = activityWaitlistIds(activityId);
    int oldCap = a->capacity;

    if (newCapacity < oldCap) {
        // 缩容：正取名额中超出容量的部分（按活跃度最低者）降级到候补
        auto ranked = oldSigned;
        std::sort(ranked.begin(), ranked.end(), [&](int x, int y) {
            bool cx = containsId(a->checkedIn, x), cy = containsId(a->checkedIn, y);
            if (cx != cy) return cx;
            if (members_[x].score != members_[y].score) return members_[x].score > members_[y].score;
            return x < y;
        });
        ranked.resize(std::min(newCapacity, static_cast<int>(ranked.size())));
        std::vector<int> keep, overflow;
        for (int mid : oldSigned) (containsId(ranked, mid) ? keep : overflow).push_back(mid);
        a->signedUp = keep;
        a->capacity = newCapacity;
        for (int mid : overflow) {
            if (findMember(mid)) a->waitlist.push({findMember(mid)->score, mid});
        }
    } else {
        // 扩容：从候补按活跃度提拔
        a->capacity = newCapacity;
        while ((int)a->signedUp.size() < newCapacity && !a->waitlist.empty())
            if (_promoteFromWaitlist(activityId) < 0) break;
    }

    std::vector<int> newSigned = a->signedUp;
    std::vector<int> newWait = activityWaitlistIds(activityId);
    int aid = activityId, nc = newCapacity;
    pushCmd([this, aid, oldSigned, oldWait, oldCap]() { _restoreSignup(aid, oldSigned, oldWait, oldCap); },
            [this, aid, newSigned, newWait, nc]() { _restoreSignup(aid, newSigned, newWait, nc); });
    return true;
}

int ClubSystem::_promoteFromWaitlist(int activityId) {
    Activity* a = findActivity(activityId);
    if (!a || a->waitlist.empty()) return -1;
    // 取出全部候补，按“当前”活跃度重新选最高者。
    // 堆内 score 是报名时刻的快照，成员活跃度可能此后变化，故不能直接信堆顶。
    std::vector<std::pair<int, int>> all = a->waitlist.snapshot();
    a->waitlist.clear();
    int best = -1;
    for (size_t i = 0; i < all.size(); ++i) {
        const Member* m = findMember(all[i].second);
        if (!m || _hasTimeConflict(m->id, activityId) || !_hasRequiredTags(m->id, *a)) continue;
        if (best < 0) { best = (int)i; continue; }
        const Member* mb = findMember(all[best].second);
        if (m->score > mb->score || (m->score == mb->score && all[i].second < all[best].second))
            best = (int)i;
    }
    if (best < 0) {
        for (const auto& p : all) if (findMember(p.second)) a->waitlist.push(p);
        return -1;
    }
    int promoted = all[best].second;
    a->signedUp.push_back(promoted);
    for (size_t i = 0; i < all.size(); ++i)
        if ((int)i != best) a->waitlist.push(all[i]);
    return promoted;
}

bool ClubSystem::_hasTimeConflict(int memberId, int activityId) const {
    const Activity* a = (activityId >= 0 && activityId < static_cast<int>(activities_.size()))
                            ? &activities_[activityId] : nullptr;
    if (!a || !a->timed()) return false;
    for (const auto& act : activities_) {
        if (!act.active || act.closed || !act.timed()) continue;
        if (act.id == activityId) continue;
        if (act.day != a->day) continue;
        if (!containsId(act.signedUp, memberId)) continue;
        // 区间重叠判断 [s1,e1) 与 [s2,e2)
        if (a->startMin < act.endMin && act.startMin < a->endMin) return true;
    }
    return false;
}

std::string ClubSystem::signup(int memberId, int activityId) {
    Member* m = findMember(memberId);
    Activity* a = findActivity(activityId);
    if (!m || !a) return "失败：成员或活动不存在";
    if (a->closed) return "失败：活动已结束";
    if (!_hasRequiredTags(memberId, *a)) return "失败：缺少活动所需标签";
    if (containsId(a->signedUp, memberId)) return "失败：已报名";
    for (auto& p : a->waitlist.snapshot()) if (p.second == memberId) return "失败：已在候补";

    if (_hasTimeConflict(memberId, activityId)) return "失败：与该成员已报名活动的时间冲突";
    Activity before = *a;
    bool waiting = static_cast<int>(a->signedUp.size()) >= a->capacity;
    if (!waiting) {
        a->signedUp.push_back(memberId);
    } else a->waitlist.push({m->score, memberId});
    Activity after = *a;
    pushCmd([this, activityId, before]() { activities_[activityId] = before; },
            [this, activityId, after]() { activities_[activityId] = after; });
    return waiting ? "成功：活动已满，已加入候补（按活跃度优先）" : "成功：已报名";
}

bool ClubSystem::_cancelSignupInternal(int memberId, int activityId) {
    Activity* a = findActivity(activityId);
    if (!a) return false;
    auto& s = a->signedUp;
    auto it = std::find(s.begin(), s.end(), memberId);
    if (it != s.end()) {
        s.erase(it);
        _promoteFromWaitlist(activityId);
        return true;
    }
    std::vector<std::pair<int, int>> kept;
    for (auto& p : a->waitlist.snapshot()) if (p.second != memberId) kept.push_back(p);
    a->waitlist.clear();
    for (auto& p : kept) a->waitlist.push(p);
    return true;
}

bool ClubSystem::cancelSignup(int memberId, int activityId) {
    Activity* a = findActivity(activityId);
    if (!a || a->closed || containsId(a->checkedIn, memberId)) return false;
    bool inSigned = containsId(a->signedUp, memberId);
    bool inWait = false;
    for (auto& p : a->waitlist.snapshot()) if (p.second == memberId) { inWait = true; break; }
    if (!inSigned && !inWait) return false;
    Activity before = *a;
    bool ok = _cancelSignupInternal(memberId, activityId);
    if (ok) {
        Activity after = *a;
        pushCmd([this, activityId, before]() { activities_[activityId] = before; },
                [this, activityId, after]() { activities_[activityId] = after; });
    }
    return ok;
}

std::vector<int> ClubSystem::activityWaitlistIds(int activityId) const {
    std::vector<int> ids;
    const Activity* a = (activityId >= 0 && activityId < static_cast<int>(activities_.size()))
                            ? &activities_[activityId] : nullptr;
    if (!a) return ids;
    for (auto& p : a->waitlist.snapshot()) ids.push_back(p.second);
    std::sort(ids.begin(), ids.end(), [this](int x, int y) {
        if (members_[x].score != members_[y].score) return members_[x].score > members_[y].score;
        return x < y;
    });
    return ids;
}
void ClubSystem::setActivityWaitlist(int activityId, const std::vector<int>& ids) {
    Activity* a = findActivity(activityId);
    if (!a) return;
    a->waitlist.clear();
    for (int mid : ids) {
        if (const Member* m = findMember(mid)) a->waitlist.push({m->score, mid});
    }
}

// ==================== 申请审批（优先级队列）====================

int ClubSystem::submitApplication(int memberId, int clubId, int priority) {
    Member* m = findMember(memberId);
    Club* c = findClub(clubId);
    if (!m || !c || containsId(c->memberIds, memberId)) return -1;
    for (const auto& p : applications_)
        if (p.status == 0 && p.memberId == memberId && p.clubId == clubId) return -1;
    Application app;
    app.id = nextAppId_++;
    app.memberId = memberId; app.clubId = clubId;
    app.submitSeq = nextSeq(); app.priority = priority; app.status = 0;
    applications_.push_back(app);
    _rebuildPendingQueue();

    int appId = app.id, prio = priority; long long seq = app.submitSeq;
    pushCmd(
        [this, appId]() {
            for (auto& a : applications_) if (a.id == appId) { a.status = -2; break; }
            _rebuildPendingQueue();
        },
        [this, appId, prio, seq]() {
            for (auto& a : applications_) if (a.id == appId) { a.status = 0; a.priority = prio; a.submitSeq = seq; break; }
            _rebuildPendingQueue();
        });
    return appId;
}

void ClubSystem::_rebuildPendingQueue() {
    pendingApps_.clear();
    for (const auto& ap : applications_) {
        if (ap.status == 0) pendingApps_.push({ap.priority, ap.submitSeq, ap.id});
    }
}

int ClubSystem::processNextApplication() {
    return pendingApps_.empty() ? -1 : pendingApps_.top().appId;
}

bool ClubSystem::reviewApplication(int appId, bool approve) {
    Application* app = nullptr;
    for (auto& a : applications_) if (a.id == appId) { app = &a; break; }
    if (!app || app->status != 0) return false;
    int memberId = app->memberId, clubId = app->clubId;
    Club* club = findClub(clubId);
    if (!club || !findMember(memberId)) return false;
    auto before = club->memberIds;
    if (approve && !containsId(club->memberIds, memberId)) club->memberIds.push_back(memberId);
    auto after = club->memberIds;
    app->status = approve ? 1 : -1;
    _rebuildPendingQueue();

    pushCmd(
        [this, appId, clubId, before]() {
            clubs_[clubId].memberIds = before;
            for (auto& a : applications_) if (a.id == appId) { a.status = 0; break; }
            _rebuildPendingQueue();
        },
        [this, appId, clubId, approve, after]() {
            clubs_[clubId].memberIds = after;
            for (auto& a : applications_) if (a.id == appId) { a.status = approve ? 1 : -1; break; }
            _rebuildPendingQueue();
        });
    return true;
}

int ClubSystem::pendingCount() const {
    return static_cast<int>(pendingApps_.size());
}

// ==================== 好友关系（图 + BFS）====================

void ClubSystem::addFriend(int a, int b) {
    if (a == b || !findMember(a) || !findMember(b)) return;
    bool before = friendGraph_.areFriends(a, b);
    friendGraph_.addFriend(a, b);
    if (!before) {
        pushCmd([this, a, b]() { friendGraph_.removeFriend(a, b); },
                [this, a, b]() { friendGraph_.addFriend(a, b); });
    }
}
bool ClubSystem::removeFriend(int a, int b) {
    bool ok = friendGraph_.removeFriend(a, b);
    if (ok) {
        pushCmd([this, a, b]() { friendGraph_.addFriend(a, b); },
                [this, a, b]() { friendGraph_.removeFriend(a, b); });
    }
    return ok;
}
std::vector<FriendGraph::Recommendation> ClubSystem::recommendFriends(int id) const {
    return friendGraph_.recommend(id);
}
int ClubSystem::friendDistance(int a, int b) const {
    if (!findMember(a) || !findMember(b)) return -1;
    return friendGraph_.friendDistance(a, b);
}
std::vector<int> ClubSystem::friendPath(int a, int b) const {
    if (!findMember(a) || !findMember(b)) return {};
    return friendGraph_.path(a, b);
}

std::vector<std::pair<int, int>> ClubSystem::recommendActivities(int id) const {
    if (!findMember(id)) return {};
    std::unordered_map<int, int> cnt;
    for (int f : friendGraph_.friendsOf(id)) {
        for (const auto& act : activities_) {
            if (act.active && containsId(act.signedUp, f)) cnt[act.id] += 1;
        }
    }
    std::vector<std::pair<int, int>> out(cnt.begin(), cnt.end());
    out.erase(std::remove_if(out.begin(), out.end(),
                             [this, id](const std::pair<int, int>& p) {
                                 const Activity* a = (p.first >= 0 && p.first < (int)activities_.size())
                                                         ? &activities_[p.first] : nullptr;
                                 return !a || a->closed || containsId(a->signedUp, id) ||
                                        containsId(activityWaitlistIds(a->id), id) ||
                                        !_hasRequiredTags(id, *a) || _hasTimeConflict(id, a->id);
                             }),
              out.end());
    std::sort(out.begin(), out.end(), [](const std::pair<int, int>& x, const std::pair<int, int>& y) {
        if (x.second != y.second) return x.second > y.second;
        return x.first < y.first;
    });
    return out;
}

// ==================== 排行榜 / 统计（AVL）====================

std::vector<std::pair<int, int>> ClubSystem::leaderboard(size_t k) const {
    return scoreIndex_.topK(k);
}
int ClubSystem::memberRank(int id) const {
    const Member* m = findMember(id);
    if (!m) return -1;
    return scoreIndex_.rankOf(m->score, id);
}
int ClubSystem::rangeActive(int lo, int hi) const {
    return scoreIndex_.rangeCount(lo, hi);
}
std::vector<std::pair<long long, int>> ClubSystem::clubLeaderboard(size_t k) const {
    std::vector<std::pair<long long, int>> out;  // 总分可能超过单个成员的 int 范围
    for (const auto& c : clubs_) {
        if (!c.active) continue;
        long long total = 0;
        for (int mid : c.memberIds) { const Member* m = findMember(mid); if (m) total += m->score; }
        out.push_back({total, c.id});
    }
    std::sort(out.begin(), out.end(), [](const std::pair<long long, int>& a, const std::pair<long long, int>& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    if (out.size() > k) out.resize(k);
    return out;
}

// ==================== 公告广播 ====================

int ClubSystem::broadcast(int clubId, int senderId, const std::string& message) {
    Club* c = findClub(clubId);
    Member* s = findMember(senderId);
    if (!c || !s || trim(message).empty()) return -1;
    Announcement an;
    an.id = nextAnnouncementId_++;
    an.clubId = clubId; an.senderId = senderId; an.message = message; an.seq = nextSeq();
    announcements_.push_back(an);
    int aid = an.id;
    Announcement snap = an;
    pushCmd(
        [this, aid]() {
            for (auto it = announcements_.begin(); it != announcements_.end(); ++it)
                if (it->id == aid) { announcements_.erase(it); break; }
        },
        [this, snap]() { announcements_.push_back(snap); });
    return an.id;
}
std::vector<Announcement> ClubSystem::announcementsOf(int clubId) const {
    std::vector<Announcement> out;
    for (auto& an : announcements_) if (an.clubId == clubId) out.push_back(an);
    return out;
}
std::vector<Announcement> ClubSystem::announcementsForMember(int memberId) const {
    std::vector<Announcement> out;
    for (auto& c : clubs_) {
        if (!c.active || !containsId(c.memberIds, memberId)) continue;
        for (auto& an : announcements_) if (an.clubId == c.id) out.push_back(an);
    }
    std::sort(out.begin(), out.end(), [](const Announcement& a, const Announcement& b) {
        return a.seq < b.seq;
    });
    return out;
}

// ==================== 撤销 / 重做 ====================

bool ClubSystem::undo() {
    if (undoStack_.empty()) return false;
    Cmd c = std::move(undoStack_.back());
    undoStack_.pop_back();
    c.undo();
    redoStack_.push_back(std::move(c));
    return true;
}
bool ClubSystem::redo() {
    if (redoStack_.empty()) return false;
    Cmd c = std::move(redoStack_.back());
    redoStack_.pop_back();
    c.redo();
    undoStack_.push_back(std::move(c));
    return true;
}

// ==================== 批量导入 ====================

ClubSystem::ImportResult ClubSystem::importMembers(const std::string& csv) {
    ImportResult r;
    std::vector<Member> added;
    std::istringstream ss(csv);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line).empty()) continue;
        std::vector<std::string> parts;
        if (!club_io::parseCsvRow(line, parts) || (parts.size() != 2 && parts.size() != 3 && parts.size() != 5)) {
            r.skippedInvalid++; continue;
        }
        if ((parts[0] == "姓名" && parts[1] == "年级") || (parts[0] == "name" && parts[1] == "grade")) continue;
        std::string name = trim(parts[0]), grade = trim(parts[1]);
        if (name.empty()) { r.skippedInvalid++; continue; }
        std::vector<std::string> tags;
        if (parts.size() >= 3) tags = parseStrs(parts[2]);
        if (memberByName_.contains(name)) { r.skippedDup++; continue; }
        Member m;
        m.id = nextMemberId_++;
        m.name = name; m.grade = grade; m.score = 0; m.joinSeq = nextSeq(); m.tags = tags;
        _addMemberInternal(m);
        added.push_back(m);
        r.added++;
    }
    if (!added.empty()) {
        pushCmd(
            [this, added]() { for (auto& m : added) _removeMemberInternal(m.id); },
            [this, added]() { for (auto& m : added) _addMemberInternal(m); });
    }
    return r;
}
