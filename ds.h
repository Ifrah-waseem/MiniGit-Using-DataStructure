// ds.h - All data structures used by Mini Git, written from scratch.
//   DynArray, LinkedList, Queue, HashTable, AVLTree, MaxHeap, quickSort
#ifndef DS_H
#define DS_H

#include <string>
using std::string;

// ---------------------------------------------------------------
// DynArray : resizable array (stores file lines, commit lists...)
// ---------------------------------------------------------------
template <class T>
class DynArray {
    T* data;
    int sz, cap;
    void grow() {
        int nc = (cap == 0) ? 4 : cap * 2;
        T* nd = new T[nc];
        for (int i = 0; i < sz; i++) nd[i] = data[i];
        delete[] data;
        data = nd;
        cap = nc;
    }
public:
    DynArray() : data(nullptr), sz(0), cap(0) {}
    DynArray(const DynArray& o) : data(nullptr), sz(0), cap(0) { *this = o; }
    DynArray& operator=(const DynArray& o) {
        if (this == &o) return *this;
        delete[] data;
        sz = o.sz;
        cap = o.sz;
        data = cap ? new T[cap] : nullptr;
        for (int i = 0; i < sz; i++) data[i] = o.data[i];
        return *this;
    }
    ~DynArray() { delete[] data; }

    void push_back(const T& v) {
        T copy = v;                 // copy first: v may live inside data
        if (sz == cap) grow();
        data[sz++] = copy;
    }
    void pop_back() { if (sz > 0) sz--; }
    void assign(int n, const T& v) {            // n copies of v
        sz = 0;
        for (int i = 0; i < n; i++) push_back(v);
    }
    void clear() { sz = 0; }
    int size() const { return sz; }
    T& operator[](int i) { return data[i]; }
    const T& operator[](int i) const { return data[i]; }
    bool operator==(const DynArray& o) const {
        if (sz != o.sz) return false;
        for (int i = 0; i < sz; i++) if (!(data[i] == o.data[i])) return false;
        return true;
    }
};

// ---------------------------------------------------------------
// LinkedList : singly linked list (commit history chain)
// ---------------------------------------------------------------
template <class T>
class LinkedList {
public:
    struct Node { T val; Node* next; Node(const T& v) : val(v), next(nullptr) {} };
    Node* head;
    Node* tail;
    int n;

    LinkedList() : head(nullptr), tail(nullptr), n(0) {}
    LinkedList(const LinkedList&) = delete;
    LinkedList& operator=(const LinkedList&) = delete;
    ~LinkedList() {
        while (head) { Node* t = head; head = head->next; delete t; }
    }
    void push_back(const T& v) {
        Node* nn = new Node(v);
        if (tail) tail->next = nn; else head = nn;
        tail = nn;
        n++;
    }
    int size() const { return n; }
};

// ---------------------------------------------------------------
// Queue : linked list based (staging area, BFS)
// ---------------------------------------------------------------
template <class T>
class Queue {
    struct Node { T val; Node* next; Node(const T& v) : val(v), next(nullptr) {} };
    Node* head;
    Node* tail;
    int n;
public:
    Queue() : head(nullptr), tail(nullptr), n(0) {}
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    ~Queue() { clear(); }

    void enqueue(const T& v) {
        Node* nn = new Node(v);
        if (tail) tail->next = nn; else head = nn;
        tail = nn;
        n++;
    }
    T dequeue() {
        Node* t = head;
        T v = t->val;
        head = head->next;
        if (!head) tail = nullptr;
        delete t;
        n--;
        return v;
    }
    bool empty() const { return n == 0; }
    int size() const { return n; }
    void clear() { while (!empty()) dequeue(); }
    bool contains(const T& v) const {
        for (Node* p = head; p; p = p->next) if (p->val == v) return true;
        return false;
    }
    template <class F>
    void each(F f) const { for (Node* p = head; p; p = p->next) f(p->val); }
};

// ---------------------------------------------------------------
// HashTable : string key -> V, separate chaining, auto resize
// ---------------------------------------------------------------
template <class V>
class HashTable {
    struct Node {
        string key; V val; Node* next;
        Node(const string& k, const V& v, Node* nx) : key(k), val(v), next(nx) {}
    };
    Node** buckets;
    int cap, n;

    static unsigned long hashStr(const string& s) {
        unsigned long h = 5381;                       // djb2
        for (unsigned char c : s) h = h * 33 + c;
        return h;
    }
    void rehash() {
        int nc = cap * 2 + 1;
        Node** nb = new Node*[nc];
        for (int i = 0; i < nc; i++) nb[i] = nullptr;
        for (int i = 0; i < cap; i++) {
            Node* p = buckets[i];
            while (p) {
                Node* nx = p->next;
                unsigned long idx = hashStr(p->key) % nc;
                p->next = nb[idx];
                nb[idx] = p;
                p = nx;
            }
        }
        delete[] buckets;
        buckets = nb;
        cap = nc;
    }
public:
    HashTable() : cap(101), n(0) {
        buckets = new Node*[cap];
        for (int i = 0; i < cap; i++) buckets[i] = nullptr;
    }
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;
    ~HashTable() {
        for (int i = 0; i < cap; i++) {
            Node* p = buckets[i];
            while (p) { Node* t = p; p = p->next; delete t; }
        }
        delete[] buckets;
    }
    V* get(const string& key) const {                 // nullptr if missing
        for (Node* p = buckets[hashStr(key) % cap]; p; p = p->next)
            if (p->key == key) return &p->val;
        return nullptr;
    }
    void put(const string& key, const V& val) {
        V* ex = get(key);
        if (ex) { *ex = val; return; }
        if (n >= cap * 3 / 4) rehash();
        unsigned long idx = hashStr(key) % cap;
        buckets[idx] = new Node(key, val, buckets[idx]);
        n++;
    }
    int size() const { return n; }
};

// ---------------------------------------------------------------
// AVLTree : self-balancing BST, string key -> V
// (index of files inside each commit snapshot)
// ---------------------------------------------------------------
template <class V>
class AVLTree {
    struct Node {
        string key; V val; Node* l; Node* r; int h;
        Node(const string& k, const V& v) : key(k), val(v), l(nullptr), r(nullptr), h(1) {}
    };
    Node* root;
    int n;

    static int height(Node* t) { return t ? t->h : 0; }
    static void update(Node* t) {
        int a = height(t->l), b = height(t->r);
        t->h = 1 + (a > b ? a : b);
    }
    static Node* rotateRight(Node* y) {
        Node* x = y->l; y->l = x->r; x->r = y;
        update(y); update(x);
        return x;
    }
    static Node* rotateLeft(Node* x) {
        Node* y = x->r; x->r = y->l; y->l = x;
        update(x); update(y);
        return y;
    }
    Node* insertRec(Node* t, const string& k, const V& v) {
        if (!t) { n++; return new Node(k, v); }
        if (k < t->key)      t->l = insertRec(t->l, k, v);
        else if (k > t->key) t->r = insertRec(t->r, k, v);
        else { t->val = v; return t; }                 // key exists: overwrite
        update(t);
        int bal = height(t->l) - height(t->r);
        if (bal > 1) {                                  // left heavy
            if (k > t->l->key) t->l = rotateLeft(t->l); // left-right case
            return rotateRight(t);
        }
        if (bal < -1) {                                 // right heavy
            if (k < t->r->key) t->r = rotateRight(t->r);
            return rotateLeft(t);
        }
        return t;
    }
    static Node* clone(Node* t) {
        if (!t) return nullptr;
        Node* c = new Node(t->key, t->val);
        c->h = t->h;
        c->l = clone(t->l);
        c->r = clone(t->r);
        return c;
    }
    static void destroy(Node* t) {
        if (!t) return;
        destroy(t->l); destroy(t->r);
        delete t;
    }
    template <class F>
    static void walk(Node* t, F& f) {                   // in-order (sorted by key)
        if (!t) return;
        walk(t->l, f);
        f(t->key, t->val);
        walk(t->r, f);
    }
public:
    AVLTree() : root(nullptr), n(0) {}
    AVLTree(const AVLTree& o) : root(clone(o.root)), n(o.n) {}
    AVLTree& operator=(const AVLTree& o) {
        if (this != &o) { destroy(root); root = clone(o.root); n = o.n; }
        return *this;
    }
    ~AVLTree() { destroy(root); }

    void insert(const string& k, const V& v) { root = insertRec(root, k, v); }
    V* find(const string& k) const {
        Node* t = root;
        while (t) {
            if (k < t->key) t = t->l;
            else if (k > t->key) t = t->r;
            else return &t->val;
        }
        return nullptr;
    }
    int size() const { return n; }
    int height() const { return height(root); }
    template <class F>
    void inorder(F f) const { walk(root, f); }          // f(const string&, V&)
};

// ---------------------------------------------------------------
// MaxHeap : binary heap in an array (needs operator< on T)
// ---------------------------------------------------------------
template <class T>
class MaxHeap {
    DynArray<T> a;
public:
    void push(const T& v) {
        a.push_back(v);
        int i = a.size() - 1;
        while (i > 0) {                                   // sift up
            int p = (i - 1) / 2;
            if (!(a[p] < a[i])) break;
            T t = a[p]; a[p] = a[i]; a[i] = t;
            i = p;
        }
    }
    T pop() {
        T top = a[0];
        a[0] = a[a.size() - 1];
        a.pop_back();
        int i = 0, n = a.size();
        while (true) {                                    // sift down
            int l = 2 * i + 1, r = l + 1, big = i;
            if (l < n && a[big] < a[l]) big = l;
            if (r < n && a[big] < a[r]) big = r;
            if (big == i) break;
            T t = a[big]; a[big] = a[i]; a[i] = t;
            i = big;
        }
        return top;
    }
    bool empty() const { return a.size() == 0; }
};

// ---------------------------------------------------------------
// quickSort : sorts a[lo..hi] using a "less than" function
// ---------------------------------------------------------------
template <class T>
void quickSort(DynArray<T>& a, int lo, int hi, bool (*less)(const T&, const T&)) {
    if (lo >= hi) return;
    T pivot = a[(lo + hi) / 2];
    int i = lo, j = hi;
    while (i <= j) {
        while (less(a[i], pivot)) i++;
        while (less(pivot, a[j])) j--;
        if (i <= j) {
            T t = a[i]; a[i] = a[j]; a[j] = t;
            i++; j--;
        }
    }
    quickSort(a, lo, j, less);
    quickSort(a, i, hi, less);
}

#endif
