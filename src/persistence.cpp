#include "system.h"
#include "data_io.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {
std::vector<std::string> split(const std::string& s, char separator) {
    std::vector<std::string> result;
    size_t begin = 0;
    for (;;) {
        size_t end = s.find(separator, begin);
        result.push_back(s.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) return result;
        begin = end + 1;
    }
}
long long number(const std::string& s) {
    if (s.empty()) throw std::runtime_error("empty number");
    size_t used = 0;
    long long value = std::stoll(s, &used);
    if (used != s.size()) throw std::runtime_error("bad number");
    return value;
}
int integer(const std::string& s) {
    long long n = number(s);
    if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) throw std::runtime_error("integer overflow");
    return static_cast<int>(n);
}
bool boolean(const std::string& s) {
    if (s != "0" && s != "1") throw std::runtime_error("bad boolean");
    return s == "1";
}
std::string encode(const std::string& s) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (c == '%' || c == '|' || c == ';' || c < 32 || c == 127) {
            out += '%'; out += hex[c >> 4]; out += hex[c & 15];
        } else out += static_cast<char>(c);
    }
    return out;
}
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw std::runtime_error("bad escape");
}
std::string decode(const std::string& s, bool v2) {
    if (!v2) return s;
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') out += s[i];
        else {
            if (i + 2 >= s.size()) throw std::runtime_error("short escape");
            out += static_cast<char>((hexDigit(s[i + 1]) << 4) | hexDigit(s[i + 2]));
            i += 2;
        }
    }
    return out;
}
std::string ids(const std::vector<int>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) { if (i) out << ' '; out << values[i]; }
    return out.str();
}
std::vector<int> readIds(const std::string& s) {
    std::istringstream in(s);
    std::vector<int> out;
    std::unordered_set<int> seen;
    std::string token;
    while (in >> token) {
        int id = integer(token);
        if (id < 0 || !seen.insert(id).second) throw std::runtime_error("bad reference list");
        out.push_back(id);
    }
    return out;
}
std::string tags(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) { if (i) out += ';'; out += encode(values[i]); }
    return out;
}
std::vector<std::string> readTags(const std::string& s, bool v2) {
    std::vector<std::string> out;
    for (const auto& t : split(s, ';')) if (!t.empty()) {
        auto tag = decode(t, v2);
        if (std::find(out.begin(), out.end(), tag) == out.end()) out.push_back(tag);
    }
    return out;
}
bool has(const std::vector<int>& v, int id) { return std::find(v.begin(), v.end(), id) != v.end(); }
void require(bool condition) { if (!condition) throw std::runtime_error("invalid save"); }
}

bool ClubSystem::save(const std::string& path) const {
    std::ostringstream out;
    out << "# ClubManager save v3\nclock|" << clock_ << "\nseq|m" << nextMemberId_
        << "|c" << nextClubId_ << "|a" << nextActivityId_ << "|p" << nextAppId_ << "|n" << nextAnnouncementId_ << '\n';
    out << "members\n";
    for (const auto& m : members_) out << "M|" << m.id << '|' << encode(m.name) << '|' << encode(m.grade)
        << '|' << m.score << '|' << m.joinSeq << '|' << m.active << '|' << tags(m.tags) << '|' << m.accountKey << '\n';
    out << "END\nclubs\n";
    for (const auto& c : clubs_) out << "C|" << c.id << '|' << encode(c.name) << '|' << encode(c.category)
        << '|' << encode(c.founded) << '|' << encode(c.description) << '|' << c.active << '|'
        << ids(c.memberIds) << '|' << c.parentClub << '|' << ids(c.subClubs) << '\n';
    out << "END\nactivities\n";
    for (const auto& a : activities_) out << "A|" << a.id << '|' << a.clubId << '|' << encode(a.name)
        << '|' << encode(a.time) << '|' << a.capacity << '|' << a.active << '|' << ids(a.signedUp)
        << '|' << ids(activityWaitlistIds(a.id)) << '|' << tags(a.requiredTags) << '|' << a.day
        << '|' << a.startMin << '|' << a.endMin << '|' << ids(a.checkedIn) << '|' << a.closed << '\n';
    out << "END\napplications\n";
    for (const auto& p : applications_) out << "P|" << p.id << '|' << p.memberId << '|' << p.clubId
        << '|' << p.submitSeq << '|' << p.status << '|' << p.priority << '\n';
    out << "END\nannouncements\n";
    for (const auto& n : announcements_) out << "N|" << n.id << '|' << n.clubId << '|' << n.senderId
        << '|' << encode(n.message) << '|' << n.seq << '\n';
    out << "END\nfriends\n";
    for (const auto& e : friendGraph_.edges()) out << "F|" << e.first << '|' << e.second << '\n';
    out << "END\n";
    return club_io::atomicWrite(path, out.str());
}

bool ClubSystem::load(const std::string& path) {
    // 先在独立对象中解析和校验；任何错误都保留当前数据及撤销历史。
    ClubSystem staged;
    try {
        std::ifstream in(std::filesystem::u8path(path));
        if (!in) return false;
        std::string line;
        if (!std::getline(in, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        bool v3 = line == "# ClubManager save v3";
        bool v2 = v3 || line == "# ClubManager save v2";
        require(v2 || line == "# ClubManager save");
        int section = -1, completed = 0;
        bool gotClock = false, gotSeq = false;
        const std::vector<std::string> sections = {"members", "clubs", "activities", "applications", "announcements", "friends"};
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line == "END") { require(section >= 0); section = -1; ++completed; continue; }
            auto name = std::find(sections.begin(), sections.end(), line);
            if (name != sections.end()) {
                require(section == -1 && name - sections.begin() == completed);
                section = completed; continue;
            }
            auto f = split(line, '|');
            auto text = [&](size_t i) { return decode(f.at(i), v2); };
            if (section == -1 && f[0] == "clock") {
                require(!gotClock && completed == 0 && f.size() == 2);
                staged.clock_ = number(f[1]); gotClock = true; require(staged.clock_ >= 0 && staged.clock_ < std::numeric_limits<long long>::max());
            } else if (section == -1 && f[0] == "seq") {
                require(!gotSeq && completed == 0 && (f.size() == 6 || (!v2 && f.size() == 5)));
                int* counters[] = {&staged.nextMemberId_, &staged.nextClubId_, &staged.nextActivityId_, &staged.nextAppId_, &staged.nextAnnouncementId_};
                const std::string prefixes = "mcapn";
                for (size_t i = 1; i < f.size(); ++i) {
                    require(f[i].size() > 1 && f[i][0] == prefixes[i - 1]);
                    *counters[i - 1] = integer(f[i].substr(1));
                    require(*counters[i - 1] >= 0 && *counters[i - 1] < std::numeric_limits<int>::max());
                }
                gotSeq = true;
            } else if (section == 0 && f[0] == "M") {
                require(v3 ? f.size() == 9 : (f.size() == 8 || (!v2 && f.size() == 7)));
                Member m; m.id = integer(f[1]); m.name = text(2); m.grade = text(3);
                m.score = integer(f[4]); m.joinSeq = number(f[5]); m.active = boolean(f[6]);
                if (f.size() > 7) m.tags = readTags(f[7], v2);
                if (v3) {
                    m.accountKey = f[8];
                    require(m.accountKey.empty() || (m.accountKey.size() == 64 && m.accountKey.find_first_not_of("0123456789abcdef") == std::string::npos));
                    if (!m.accountKey.empty()) for (const auto& other : staged.members_) require(other.accountKey != m.accountKey);
                }
                require(m.id == static_cast<int>(staged.members_.size()) && m.score >= 0 && m.joinSeq >= 0 && m.joinSeq <= staged.clock_);
                require(!m.active || m.name.find_first_not_of(" \t\r\n") != std::string::npos);
                require(!m.active || !staged.memberByName_.contains(m.name));
                staged.members_.push_back(m);
                if (m.active) staged.memberByName_.put(m.name, m.id);
            } else if (section == 1 && f[0] == "C") {
                require(f.size() >= 7 && f.size() <= 10 && (!v2 || f.size() == 10));
                Club c; c.id = integer(f[1]); c.name = text(2); c.category = text(3);
                c.founded = text(4); c.description = text(5); c.active = boolean(f[6]);
                if (f.size() > 7) c.memberIds = readIds(f[7]);
                if (f.size() > 8) c.parentClub = integer(f[8]);
                if (f.size() > 9) c.subClubs = readIds(f[9]);
                require(c.id == static_cast<int>(staged.clubs_.size()) && c.parentClub >= -1);
                require(!c.active || (c.name.find_first_not_of(" \t\r\n") != std::string::npos && !staged.clubByName_.contains(c.name)));
                staged.clubs_.push_back(c);
                if (c.active) staged.clubByName_.put(c.name, c.id);
            } else if (section == 2 && f[0] == "A") {
                require(v2 ? f.size() == 15 : (f.size() == 9 || f.size() == 13));
                Activity a; a.id = integer(f[1]); a.clubId = integer(f[2]); a.name = text(3);
                a.time = text(4); a.capacity = integer(f[5]); a.active = boolean(f[6]); a.signedUp = readIds(f[7]);
                for (int id : readIds(f[8])) { require(id < static_cast<int>(staged.members_.size())); a.waitlist.push({staged.members_[id].score, id}); }
                if (f.size() > 9) { a.requiredTags = readTags(f[9], v2); a.day = integer(f[10]); a.startMin = integer(f[11]); a.endMin = integer(f[12]); }
                if (v2) { a.checkedIn = readIds(f[13]); a.closed = boolean(f[14]); }
                require(a.id == static_cast<int>(staged.activities_.size()) && a.capacity >= 0 && a.signedUp.size() <= static_cast<size_t>(a.capacity));
                require((a.day == -1 && a.startMin == -1 && a.endMin == -1) ||
                        (a.day >= 0 && a.day <= 6 && a.startMin >= 0 && a.startMin < a.endMin && a.endMin <= 1440));
                require(!a.active || (a.name.find_first_not_of(" \t\r\n") != std::string::npos && !staged.activityByName_.contains(a.name)));
                staged.activities_.push_back(a);
                if (a.active) staged.activityByName_.put(a.name, a.id);
            } else if (section == 3 && f[0] == "P") {
                require(f.size() == 7 || (!v2 && f.size() == 6));
                Application p; p.id = integer(f[1]); p.memberId = integer(f[2]); p.clubId = integer(f[3]);
                p.submitSeq = number(f[4]); p.status = integer(f[5]); if (f.size() > 6) p.priority = integer(f[6]);
                require(p.id == static_cast<int>(staged.applications_.size()) && p.status >= -2 && p.status <= 1);
                require(p.submitSeq >= 0 && p.submitSeq <= staged.clock_);
                staged.applications_.push_back(p);
            } else if (section == 4 && f[0] == "N") {
                require(f.size() == 6);
                Announcement n; n.id = integer(f[1]); n.clubId = integer(f[2]); n.senderId = integer(f[3]); n.message = text(4); n.seq = number(f[5]);
                require(n.id >= 0 && n.id < staged.nextAnnouncementId_ && n.seq >= 0 && n.seq <= staged.clock_);
                for (const auto& old : staged.announcements_) require(old.id != n.id);
                staged.announcements_.push_back(n);
            } else if (section == 5 && f[0] == "F") {
                require(f.size() == 3);
                int a = integer(f[1]), b = integer(f[2]);
                require(a != b && staged.findMember(a) && staged.findMember(b));
                require(!staged.friendGraph_.areFriends(a, b));
                staged.friendGraph_.addFriend(a, b);
            } else require(false);
        }
        require(!in.bad() && section == -1 && completed == 6 && gotClock && gotSeq);
        require(staged.nextMemberId_ == static_cast<int>(staged.members_.size()) && staged.nextClubId_ == static_cast<int>(staged.clubs_.size()));
        require(staged.nextActivityId_ == static_cast<int>(staged.activities_.size()) && staged.nextAppId_ == static_cast<int>(staged.applications_.size()));
        auto memberRef = [&](int id) { return id >= 0 && id < static_cast<int>(staged.members_.size()); };
        auto clubRef = [&](int id) { return id >= 0 && id < static_cast<int>(staged.clubs_.size()); };
        for (const auto& c : staged.clubs_) {
            for (int id : c.memberIds) require(memberRef(id) && (!c.active || staged.findMember(id)));
            for (int id : c.subClubs) require(clubRef(id) && staged.clubs_[id].parentClub == c.id);
            if (c.parentClub >= 0) require(clubRef(c.parentClub));
            if (!c.active) continue;
            if (c.parentClub >= 0) require(staged.findClub(c.parentClub) && has(staged.clubs_[c.parentClub].subClubs, c.id));
            int id = c.id;
            size_t depth = 0;
            while (id >= 0) { require(clubRef(id) && ++depth <= staged.clubs_.size()); id = staged.clubs_[id].parentClub; }
        }
        for (const auto& a : staged.activities_) {
            require(clubRef(a.clubId) && (!a.active || staged.findClub(a.clubId)));
            for (int id : a.signedUp) require(memberRef(id) && (!a.active || staged.findMember(id)));
            for (int id : staged.activityWaitlistIds(a.id)) require(memberRef(id) && !has(a.signedUp, id) && (!a.active || staged.findMember(id)));
            for (int id : a.checkedIn) require(has(a.signedUp, id));
            if (a.active && !a.closed) for (int id : a.signedUp) require(!staged._hasTimeConflict(id, a.id));
        }
        for (const auto& p : staged.applications_) {
            require(memberRef(p.memberId) && clubRef(p.clubId));
            if (p.status == 0) require(staged.findMember(p.memberId) && staged.findClub(p.clubId));
        }
        for (const auto& n : staged.announcements_) require(memberRef(n.senderId) && clubRef(n.clubId));
    } catch (const std::exception&) { return false; }

    members_ = std::move(staged.members_); clubs_ = std::move(staged.clubs_);
    activities_ = std::move(staged.activities_); applications_ = std::move(staged.applications_);
    announcements_ = std::move(staged.announcements_); friendGraph_ = std::move(staged.friendGraph_);
    clock_ = staged.clock_; nextMemberId_ = staged.nextMemberId_; nextClubId_ = staged.nextClubId_;
    nextActivityId_ = staged.nextActivityId_; nextAppId_ = staged.nextAppId_; nextAnnouncementId_ = staged.nextAnnouncementId_;
    undoStack_.clear(); redoStack_.clear();
    memberByName_.clear(); clubByName_.clear(); activityByName_.clear(); scoreIndex_.clear(); tagIndex_.clear();
    for (const auto& m : members_) if (m.active) { memberByName_.put(m.name, m.id); scoreIndex_.insert(m.score, m.id); _addTagToIndex(m); }
    for (const auto& c : clubs_) if (c.active) clubByName_.put(c.name, c.id);
    for (const auto& a : activities_) if (a.active) activityByName_.put(a.name, a.id);
    _rebuildPendingQueue();
    return true;
}
