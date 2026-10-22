#ifndef CONTAINERS_H
#define CONTAINERS_H

#include <vector>
#include <stdexcept>

// 顺序栈（基于 vector），用于撤销操作历史等。O(1) 入栈/出栈。
template <typename T>
class Stack {
public:
    void push(const T& v) { data_.push_back(v); }
    void pop() {
        if (data_.empty()) throw std::out_of_range("Stack: pop from empty");
        data_.pop_back();
    }
    const T& top() const {
        if (data_.empty()) throw std::out_of_range("Stack: top from empty");
        return data_.back();
    }
    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }

private:
    std::vector<T> data_;
};

// 循环队列（定长数组实现），用于审批队列等 FIFO 场景。O(1) 入队/出队。
template <typename T>
class Queue {
public:
    explicit Queue(size_t capacity = 16) : buf_(capacity ? capacity : 1), cap_(capacity ? capacity : 1) {}

    bool push(const T& v) {
        if (size_ == cap_) {
            if (!grow()) return false;
        }
        buf_[tail_] = v;
        tail_ = (tail_ + 1) % cap_;
        ++size_;
        return true;
    }

    bool pop() {
        if (size_ == 0) return false;
        head_ = (head_ + 1) % cap_;
        --size_;
        return true;
    }

    const T& front() const {
        if (size_ == 0) throw std::out_of_range("Queue: front from empty");
        return buf_[head_];
    }
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    size_t capacity() const { return cap_; }
    void clear() { head_ = 0; tail_ = 0; size_ = 0; }

private:
    bool grow() {
        std::vector<T> nb(cap_ * 2);
        for (size_t i = 0; i < size_; ++i) {
            nb[i] = buf_[(head_ + i) % cap_];
        }
        buf_.swap(nb);
        head_ = 0;
        tail_ = size_;
        cap_ *= 2;
        return true;
    }

    std::vector<T> buf_;
    size_t head_ = 0, tail_ = 0, size_ = 0, cap_;
};

#endif // CONTAINERS_H
