#include "inverted_index.h"

#include <algorithm>

void InvertedIndex::add(const std::string& term, int docId) {
    auto& list = index_[term];
    // 保持升序且去重
    auto it = std::lower_bound(list.begin(), list.end(), docId);
    if (it == list.end() || *it != docId) list.insert(it, docId);
}

void InvertedIndex::remove(const std::string& term, int docId) {
    auto it = index_.find(term);
    if (it == index_.end()) return;
    auto& list = it->second;
    auto jt = std::lower_bound(list.begin(), list.end(), docId);
    if (jt != list.end() && *jt == docId) list.erase(jt);
    if (list.empty()) index_.erase(it);  // 空倒排表回收
}

void InvertedIndex::removeDoc(int docId) {
    for (auto it = index_.begin(); it != index_.end();) {
        auto& list = it->second;
        auto jt = std::lower_bound(list.begin(), list.end(), docId);
        if (jt != list.end() && *jt == docId) list.erase(jt);
        it = list.empty() ? index_.erase(it) : std::next(it);
    }
}

bool InvertedIndex::contains(const std::string& term, int docId) const {
    auto it = index_.find(term);
    if (it == index_.end()) return false;
    const auto& list = it->second;
    return std::binary_search(list.begin(), list.end(), docId);
}

std::vector<int> InvertedIndex::query(const std::string& term) const {
    auto it = index_.find(term);
    if (it == index_.end()) return {};
    return it->second;  // 已是升序
}

// 有序归并求交：同时出现在所有倒排表中的文档
std::vector<int> InvertedIndex::queryAnd(const std::vector<std::string>& terms) const {
    if (terms.empty()) return {};
    // 从最短的倒排表开始，逐词求交，减少比较量
    std::vector<std::string> sorted = terms;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::vector<int> result;
    bool first = true;
    for (const auto& t : sorted) {
        auto it = index_.find(t);
        if (it == index_.end()) return {};  // 任一词无人具备 → 交集为空
        const auto& list = it->second;
        if (first) {
            result = list;
            first = false;
        } else {
            // 两路归并求交
            std::vector<int> merged;
            merged.reserve(std::min(result.size(), list.size()));
            size_t i = 0, j = 0;
            while (i < result.size() && j < list.size()) {
                if (result[i] == list[j]) { merged.push_back(result[i]); ++i; ++j; }
                else if (result[i] < list[j]) ++i;
                else ++j;
            }
            result.swap(merged);
            if (result.empty()) return {};
        }
    }
    return result;
}

// 有序归并求并：出现在任一倒排表中的文档（去重）
std::vector<int> InvertedIndex::queryOr(const std::vector<std::string>& terms) const {
    std::vector<int> result;
    for (const auto& t : terms) {
        auto it = index_.find(t);
        if (it == index_.end()) continue;
        // 归并 result 与 list（均升序）
        std::vector<int> merged;
        merged.reserve(result.size() + it->second.size());
        std::merge(result.begin(), result.end(),
                   it->second.begin(), it->second.end(),
                   std::back_inserter(merged));
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        result.swap(merged);
    }
    return result;
}

std::vector<std::string> InvertedIndex::terms() const {
    std::vector<std::string> out;
    out.reserve(index_.size());
    for (const auto& kv : index_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

size_t InvertedIndex::docCount(const std::string& term) const {
    auto it = index_.find(term);
    return it == index_.end() ? 0 : it->second.size();
}
