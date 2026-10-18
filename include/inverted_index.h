#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

// 倒排索引：词条 → 有序文档列表（posting list）。
// 这是搜索引擎 / 推荐系统的核心结构。这里用「哈希表 + 有序向量」实现，
// 多词条查询用「归并（merge）」求交集 / 并集，时间复杂度 O(总词表长度)。
class InvertedIndex {
public:
    // 建立 词条→文档 的索引（同词条同文档只记一次）
    void add(const std::string& term, int docId);

    // 删除单个 词条→文档 映射
    void remove(const std::string& term, int docId);

    // 从所有词条中移除某文档（删除成员时调用）
    void removeDoc(int docId);

    bool contains(const std::string& term, int docId) const;

    // 单词条的倒排表（升序）
    std::vector<int> query(const std::string& term) const;

    // 多词条 AND：同时具备所有标签的人（有序归并求交）
    std::vector<int> queryAnd(const std::vector<std::string>& terms) const;

    // 多词条 OR：具备任意一个标签的人（有序归并求并）
    std::vector<int> queryOr(const std::vector<std::string>& terms) const;

    std::vector<std::string> terms() const;   // 所有词条（升序）
    size_t docCount(const std::string& term) const;
    size_t termCount() const { return index_.size(); }
    bool empty() const { return index_.empty(); }
    void clear() { index_.clear(); }

private:
    std::unordered_map<std::string, std::vector<int>> index_;
};

#endif // INVERTED_INDEX_H
