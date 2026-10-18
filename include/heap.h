#ifndef HEAP_H
#define HEAP_H

#include <vector>
#include <functional>
#include <stdexcept>
#include <utility>

// 二叉堆（数组实现），默认最大堆。演示优先队列的 O(log n) 入堆/出堆。
// 本工具用于：活动候补名单（按成员活跃度优先）、Top-K 选拔。
template <typename T, typename Compare = std::less<T>>
class BinaryHeap {
public:
    BinaryHeap() = default;
    explicit BinaryHeap(Compare comp) : comp_(comp) {}

    void push(const T& v) {
        data_.push_back(v);
        siftUp(data_.size() - 1);
    }

    void pop() {
        if (data_.empty()) throw std::out_of_range("Heap: pop from empty");
        data_[0] = data_.back();
        data_.pop_back();
        if (!data_.empty()) siftDown(0);
    }

    const T& top() const {
        if (data_.empty()) throw std::out_of_range("Heap: top from empty");
        return data_[0];
    }

    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }

    // 返回内部元素的副本（供持久化等只读场景，不改变堆）
    std::vector<T> snapshot() const { return data_; }

private:
    void siftUp(size_t i) {
        while (i > 0) {
            size_t p = (i - 1) / 2;
            if (comp_(data_[i], data_[p])) {  // i 优先级低于父，停止
                break;
            }
            std::swap(data_[i], data_[p]);
            i = p;
        }
    }

    void siftDown(size_t i) {
        size_t n = data_.size();
        while (true) {
            size_t l = 2 * i + 1;
            size_t r = 2 * i + 2;
            size_t best = i;
            if (l < n && !comp_(data_[l], data_[best])) best = l;
            if (r < n && !comp_(data_[r], data_[best])) best = r;
            if (best == i) break;
            std::swap(data_[i], data_[best]);
            i = best;
        }
    }

    std::vector<T> data_;
    Compare comp_;
};

#endif // HEAP_H
