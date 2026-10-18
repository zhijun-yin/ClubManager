#ifndef AVL_H
#define AVL_H

#include <vector>
#include <utility>

// AVL 平衡二叉搜索树，关键字为 (score, id) 二元组（id 作唯一区分）。
// 作为成员的“有序索引”：支持按活跃度排名、Top-K、区间计数，增删查均为 O(log n)。
class AVL {
public:
    AVL() : root_(nullptr), size_(0) {}
    ~AVL() { clear(root_); }

    AVL(const AVL&) = delete;
    AVL& operator=(const AVL&) = delete;

    // 插入一个 (score, id)；若 (score,id) 已存在则忽略。O(log n)
    void insert(int score, int id);

    // 删除一个 (score, id)。O(log n)
    void remove(int score, int id);

    bool contains(int score, int id) const;

    // 返回按 score 从高到低排序的前 k 个 (score, id)。O(k + log n)
    std::vector<std::pair<int, int>> topK(size_t k) const;

    // 返回 (score, id) 的排名：有多少成员严格排在它前面（score 更高，或同分 id 更小）。O(log n)
    int rankOf(int score, int id) const;

    // 统计 score 在 [lo, hi] 区间内的成员数量。O(log n)
    int rangeCount(int lo, int hi) const;

    // 按 score 升序返回全部 (score, id)。O(n)
    std::vector<std::pair<int, int>> inorder() const;

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // 清空整棵树
    void clear() { clear(root_); root_ = nullptr; size_ = 0; }

private:
    struct Node {
        int score;
        int id;
        int height = 1;
        int cnt = 1;      // 子树节点数
        Node* left = nullptr;
        Node* right = nullptr;
        Node(int s, int i) : score(s), id(i) {}
    };

    static int height(Node* n) { return n ? n->height : 0; }
    static int cnt(Node* n) { return n ? n->cnt : 0; }
    static void update(Node* n) {
        n->height = 1 + std::max(height(n->left), height(n->right));
        n->cnt = 1 + cnt(n->left) + cnt(n->right);
    }
    static int balanceFactor(Node* n) {
        return n ? height(n->left) - height(n->right) : 0;
    }

    static Node* rotateRight(Node* y);
    static Node* rotateLeft(Node* x);
    static Node* rebalance(Node* n);

    static Node* insert(Node* node, int score, int id, bool& added);
    static Node* remove(Node* node, int score, int id, bool& removed);
    static Node* minNode(Node* node);

    void topK(Node* node, size_t k, std::vector<std::pair<int, int>>& out) const;
    int countGreater(Node* node, int score, int id) const;
    int rangeCount(Node* node, int lo, int hi) const;
    void inorder(Node* node, std::vector<std::pair<int, int>>& out) const;
    static void clear(Node* node);

    Node* root_;
    size_t size_;
};

#endif // AVL_H
