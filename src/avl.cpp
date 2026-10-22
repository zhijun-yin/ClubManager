#include "avl.h"

#include <algorithm>

// ---- 旋转与再平衡 ----

AVL::Node* AVL::rotateRight(Node* y) {
    Node* x = y->left;
    y->left = x->right;
    x->right = y;
    update(y);
    update(x);
    return x;
}

AVL::Node* AVL::rotateLeft(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    y->left = x;
    update(x);
    update(y);
    return y;
}

AVL::Node* AVL::rebalance(Node* n) {
    update(n);
    int bf = balanceFactor(n);
    if (bf > 1) {  // 左重
        if (balanceFactor(n->left) < 0) {
            n->left = rotateLeft(n->left);  // 左右情况
        }
        return rotateRight(n);
    }
    if (bf < -1) {  // 右重
        if (balanceFactor(n->right) > 0) {
            n->right = rotateRight(n->right);  // 右左情况
        }
        return rotateLeft(n);
    }
    return n;
}

// ---- 插入 ----

AVL::Node* AVL::insert(Node* node, int score, int id, bool& added) {
    if (node == nullptr) {
        added = true;
        return new Node(score, id);
    }
    if (std::make_pair(score, id) < std::make_pair(node->score, node->id)) {
        node->left = insert(node->left, score, id, added);
    } else if (std::make_pair(node->score, node->id) < std::make_pair(score, id)) {
        node->right = insert(node->right, score, id, added);
    } else {
        added = false;  // 已存在
        return node;
    }
    return rebalance(node);
}

void AVL::insert(int score, int id) {
    bool added = false;
    root_ = insert(root_, score, id, added);
    if (added) ++size_;
}

// ---- 删除 ----

AVL::Node* AVL::minNode(Node* node) {
    while (node->left) node = node->left;
    return node;
}

AVL::Node* AVL::remove(Node* node, int score, int id, bool& removed) {
    if (node == nullptr) {
        removed = false;
        return nullptr;
    }
    if (std::make_pair(score, id) < std::make_pair(node->score, node->id)) {
        node->left = remove(node->left, score, id, removed);
    } else if (std::make_pair(node->score, node->id) < std::make_pair(score, id)) {
        node->right = remove(node->right, score, id, removed);
    } else {
        removed = true;
        if (!node->left) {
            Node* r = node->right;
            delete node;
            return r;
        }
        if (!node->right) {
            Node* l = node->left;
            delete node;
            return l;
        }
        Node* succ = minNode(node->right);
        node->score = succ->score;
        node->id = succ->id;
        bool dummy = false;
        node->right = remove(node->right, succ->score, succ->id, dummy);
    }
    return rebalance(node);
}

void AVL::remove(int score, int id) {
    bool removed = false;
    root_ = remove(root_, score, id, removed);
    if (removed) --size_;
}

bool AVL::contains(int score, int id) const {
    Node* cur = root_;
    while (cur) {
        if (std::make_pair(score, id) == std::make_pair(cur->score, cur->id)) return true;
        if (std::make_pair(score, id) < std::make_pair(cur->score, cur->id))
            cur = cur->left;
        else
            cur = cur->right;
    }
    return false;
}

// ---- Top-K（按 score 降序：右-根-左的逆中序）----

void AVL::topK(Node* node, size_t k, std::vector<std::pair<int, int>>& out) const {
    if (node == nullptr || out.size() >= k) return;
    topK(node->right, k, out);  // 先访问更大的
    if (out.size() >= k) return;
    out.emplace_back(node->score, node->id);
    topK(node->left, k, out);
}

std::vector<std::pair<int, int>> AVL::topK(size_t k) const {
    std::vector<std::pair<int, int>> out;
    if (k == 0) return out;
    out.reserve(std::min(k, size_));
    topK(root_, k, out);
    return out;
}

// ---- 排名：严格大于 (score,id) 的节点数 ----

int AVL::countGreater(Node* node, int score, int id) const {
    if (node == nullptr) return 0;
    if (std::make_pair(score, id) < std::make_pair(node->score, node->id)) {
        // 当前节点及其右子树都大于 key
        return cnt(node->right) + 1 + countGreater(node->left, score, id);
    }
    // 当前节点 <= key，去右子树找
    return countGreater(node->right, score, id);
}

int AVL::rankOf(int score, int id) const {
    return countGreater(root_, score, id);
}

// ---- 区间计数：lo <= score <= hi ----

int AVL::rangeCount(Node* node, int lo, int hi) const {
    if (lo > hi) return 0;
    // 两次前缀计数，各只沿一条根到叶路径，避免遍历整个区间。
    auto prefix = [](Node* p, int bound, bool inclusive) {
        int count = 0;
        while (p) {
            if (p->score < bound || (inclusive && p->score == bound)) {
                count += cnt(p->left) + 1;
                p = p->right;
            } else p = p->left;
        }
        return count;
    };
    return prefix(node, hi, true) - prefix(node, lo, false);
}

int AVL::rangeCount(int lo, int hi) const {
    return rangeCount(root_, lo, hi);
}

// ---- 中序遍历（score 升序）----

void AVL::inorder(Node* node, std::vector<std::pair<int, int>>& out) const {
    if (!node) return;
    inorder(node->left, out);
    out.emplace_back(node->score, node->id);
    inorder(node->right, out);
}

std::vector<std::pair<int, int>> AVL::inorder() const {
    std::vector<std::pair<int, int>> out;
    out.reserve(size_);
    inorder(root_, out);
    return out;
}

void AVL::clear(Node* node) {
    if (!node) return;
    clear(node->left);
    clear(node->right);
    delete node;
}
