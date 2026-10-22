#include "system.h"
#include "data_io.h"
#include <algorithm>
#include <limits>
#include <sstream>
#include <tuple>

namespace {
bool has(const std::vector<int>& ids, int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
}

bool ClubSystem::_hasRequiredTags(int memberId, const Activity& activity) const {
    const Member* m = findMember(memberId);
    if (!m) return false;
    for (const auto& tag : activity.requiredTags)
        if (std::find(m->tags.begin(), m->tags.end(), tag) == m->tags.end()) return false;
    return true;
}

bool ClubSystem::checkIn(int memberId, int activityId) {
    Activity* a = findActivity(activityId);
    Member* m = findMember(memberId);
    if (!a || !m || a->closed || !has(a->signedUp, memberId) || has(a->checkedIn, memberId)) return false;
    if (m->score > std::numeric_limits<int>::max() - 10) return false;
    auto before = a->checkedIn;
    int oldScore = m->score, newScore = oldScore + 10;
    a->checkedIn.push_back(memberId);
    auto after = a->checkedIn;
    _setScoreInternal(memberId, newScore);
    pushCmd([this, activityId, memberId, before, oldScore]() {
        activities_[activityId].checkedIn = before;
        _setScoreInternal(memberId, oldScore);
    }, [this, activityId, memberId, after, newScore]() {
        activities_[activityId].checkedIn = after;
        _setScoreInternal(memberId, newScore);
    });
    return true;
}

bool ClubSystem::closeActivity(int activityId) {
    Activity* a = findActivity(activityId);
    if (!a || a->closed) return false;
    a->closed = true;
    pushCmd([this, activityId]() { activities_[activityId].closed = false; },
            [this, activityId]() { activities_[activityId].closed = true; });
    return true;
}

bool ClubSystem::withdrawApplication(int appId, int memberId) {
    for (auto& app : applications_) {
        if (app.id != appId || app.memberId != memberId || app.status != 0) continue;
        app.status = -2;
        _rebuildPendingQueue();
        pushCmd([this, appId]() { applications_[appId].status = 0; _rebuildPendingQueue(); },
                [this, appId]() { applications_[appId].status = -2; _rebuildPendingQueue(); });
        return true;
    }
    return false;
}

std::vector<int> ClubSystem::memberSchedule(int memberId) const {
    std::vector<int> result;
    if (!findMember(memberId)) return result;
    for (const auto& a : activities_) {
        if (!a.active) continue;
        if (has(a.signedUp, memberId) || has(activityWaitlistIds(a.id), memberId)) result.push_back(a.id);
    }
    std::sort(result.begin(), result.end(), [this](int x, int y) {
        const auto& a = activities_[x]; const auto& b = activities_[y];
        return std::make_tuple(a.closed, !a.timed(), a.day, a.startMin, a.id)
             < std::make_tuple(b.closed, !b.timed(), b.day, b.startMin, b.id);
    });
    return result;
}

ClubSystem::Statistics ClubSystem::statistics() const {
    Statistics s;
    for (const auto& m : members_) if (m.active) { ++s.members; s.totalScore += m.score; }
    for (const auto& c : clubs_) if (c.active) ++s.clubs;
    for (const auto& a : activities_) if (a.active) {
        ++s.activities;
        if (a.closed) ++s.closedActivities;
        s.registrations += static_cast<int>(a.signedUp.size());
        s.waiting += static_cast<int>(a.waitlist.size());
        s.checkIns += static_cast<int>(a.checkedIn.size());
    }
    s.pending = pendingCount();
    return s;
}

std::string ClubSystem::exportMembersCsv() const {
    std::ostringstream out;
    out << "\xEF\xBB\xBF姓名,年级,标签,ID,活跃度\r\n";
    for (const auto& m : members_) if (m.active) {
        std::string tags;
        for (const auto& t : m.tags) { if (!tags.empty()) tags += ';'; tags += t; }
        out << club_io::csvCell(m.name) << ',' << club_io::csvCell(m.grade) << ','
            << club_io::csvCell(tags) << ',' << m.id << ',' << m.score << "\r\n";
    }
    return out.str();
}

std::string ClubSystem::exportRosterCsv(int activityId) const {
    if (activityId < 0 || activityId >= static_cast<int>(activities_.size()) || !activities_[activityId].active) return "";
    const auto& a = activities_[activityId];
    std::ostringstream out;
    out << "\xEF\xBB\xBF活动,成员ID,姓名,年级,状态,签到,活跃度\r\n";
    auto row = [&](int id, const std::string& status) {
        const Member& m = members_[id];
        out << club_io::csvCell(a.name) << ',' << id << ',' << club_io::csvCell(m.name) << ','
            << club_io::csvCell(m.grade) << ',' << status << ',' << (has(a.checkedIn, id) ? "已签到" : "未签到")
            << ',' << m.score << "\r\n";
    };
    for (int id : a.signedUp) row(id, "正取");
    for (int id : activityWaitlistIds(activityId)) row(id, "候补");
    return out.str();
}
