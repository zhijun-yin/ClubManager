#ifndef HASH_MAP_H
#define HASH_MAP_H

#include <string>
#include <vector>
#include <utility>
#include <cstddef>

// 哈希表：链地址法解决冲突，键为字符串、值为 int（通常是成员/社团 id）。
// 演示平均 O(1) 的查找/插入/删除。
class StringIntMap {
public:
    // 显式指定桶数，便于测试扩容行为
    explicit StringIntMap(size_t numBuckets = 16);

    // 插入或更新键值对
    void put(const std::string& key, int value);

    // 查找，返回是否存在，并带出 value
    bool get(const std::string& key, int& value) const;

    bool contains(const std::string& key) const;

    // 删除键，返回是否删除成功
    bool remove(const std::string& key);

    // 清空全部键值对
    void clear() {
        for (auto& c : buckets_) c.clear();
        size_ = 0;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // 当前负载因子（元素数 / 桶数）
    double loadFactor() const {
        return static_cast<double>(size_) / static_cast<double>(buckets_.size());
    }

    // 触发一次扩容（桶数翻倍 + rehash）
    void rehash();

    // 返回所有键值对（顺序不确定）
    std::vector<std::pair<std::string, int>> entries() const;

private:
    size_t bucketIndex(const std::string& key) const {
        return std::hash<std::string>{}(key) % buckets_.size();
    }

    std::vector<std::vector<std::pair<std::string, int>>> buckets_;
    size_t size_ = 0;
};

#endif // HASH_MAP_H
