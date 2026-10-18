#include "hash_map.h"

#include <algorithm>

StringIntMap::StringIntMap(size_t numBuckets) {
    if (numBuckets == 0) numBuckets = 1;
    buckets_.resize(numBuckets);
}

void StringIntMap::put(const std::string& key, int value) {
    // 负载因子过高则扩容
    if (loadFactor() > 0.75) {
        rehash();
    }
    size_t b = bucketIndex(key);
    auto& chain = buckets_[b];
    for (auto& kv : chain) {
        if (kv.first == key) {
            kv.second = value;  // 更新
            return;
        }
    }
    chain.emplace_back(key, value);
    ++size_;
}

bool StringIntMap::get(const std::string& key, int& value) const {
    size_t b = bucketIndex(key);
    const auto& chain = buckets_[b];
    for (const auto& kv : chain) {
        if (kv.first == key) {
            value = kv.second;
            return true;
        }
    }
    return false;
}

bool StringIntMap::contains(const std::string& key) const {
    int tmp;
    return get(key, tmp);
}

bool StringIntMap::remove(const std::string& key) {
    size_t b = bucketIndex(key);
    auto& chain = buckets_[b];
    for (size_t i = 0; i < chain.size(); ++i) {
        if (chain[i].first == key) {
            chain.erase(chain.begin() + i);
            --size_;
            return true;
        }
    }
    return false;
}

void StringIntMap::rehash() {
    std::vector<std::vector<std::pair<std::string, int>>> old;
    old.swap(buckets_);
    buckets_.resize(old.size() * 2);
    for (auto& chain : old) {
        for (auto& kv : chain) {
            size_t b = bucketIndex(kv.first);
            buckets_[b].push_back(kv);
        }
    }
}

std::vector<std::pair<std::string, int>> StringIntMap::entries() const {
    std::vector<std::pair<std::string, int>> all;
    all.reserve(size_);
    for (const auto& chain : buckets_) {
        for (const auto& kv : chain) {
            all.push_back(kv);
        }
    }
    return all;
}

