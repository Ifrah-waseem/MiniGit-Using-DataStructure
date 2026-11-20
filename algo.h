// algo.h - String algorithms (naive, KMP, Rabin-Karp) and LCS based diff.
#ifndef ALGO_H
#define ALGO_H

#include <iostream>
#include <string>
#include "ds.h"
using std::string;

// Each function returns the index of the first match at or after 'start',
// or -1 if there is no match.

// 1) Naive search: O(n*m) in the worst case
inline int naiveFind(const string& t, const string& p, int start = 0) {
    int n = t.size(), m = p.size();
    if (m == 0) return -1;
    for (int i = start; i + m <= n; i++) {
        int j = 0;
        while (j < m && t[i + j] == p[j]) j++;
        if (j == m) return i;
    }
    return -1;
}

// 2) KMP: builds a failure table so the text is never re-scanned, O(n+m)
inline int kmpFind(const string& t, const string& p, int start = 0) {
    int n = t.size(), m = p.size();
    if (m == 0) return -1;
    DynArray<int> fail;
    fail.assign(m, 0);
    for (int i = 1, k = 0; i < m; i++) {
        while (k > 0 && p[i] != p[k]) k = fail[k - 1];
        if (p[i] == p[k]) k++;
        fail[i] = k;
    }
    for (int i = start, j = 0; i < n; i++) {
        while (j > 0 && t[i] != p[j]) j = fail[j - 1];
        if (t[i] == p[j]) j++;
        if (j == m) return i - m + 1;
    }
    return -1;
}

// 3) Rabin-Karp: rolling hash of the window, compare characters only if hashes match
inline int rabinKarpFind(const string& t, const string& p, int start = 0) {
    const long long B = 256, M = 1000000007LL;
    int n = t.size(), m = p.size();
    if (m == 0 || start + m > n) return -1;
    long long hp = 0, ht = 0, high = 1;                 // high = B^(m-1) % M
    for (int i = 0; i < m - 1; i++) high = high * B % M;
    for (int i = 0; i < m; i++) {
        hp = (hp * B + (unsigned char)p[i]) % M;
        ht = (ht * B + (unsigned char)t[start + i]) % M;
    }
    for (int i = start; ; i++) {
        if (hp == ht && t.compare(i, m, p) == 0) return i;
        if (i + m >= n) break;
        ht = ((ht - (unsigned char)t[i] * high % M + M) * B + (unsigned char)t[i + m]) % M;
    }
    return -1;
}

// LCS based line diff. Prints "- old line" / "+ new line" with line numbers.
// Returns the number of changed lines.
inline int lcsDiff(const DynArray<string>& a, const DynArray<string>& b) {
    int n = a.size(), m = b.size();
    // L[i][j] = length of LCS of a[i..] and b[j..]   (stored in a flat array)
    DynArray<int> L;
    L.assign((n + 1) * (m + 1), 0);
    for (int i = n - 1; i >= 0; i--)
        for (int j = m - 1; j >= 0; j--) {
            if (a[i] == b[j]) L[i * (m + 1) + j] = L[(i + 1) * (m + 1) + j + 1] + 1;
            else {
                int down = L[(i + 1) * (m + 1) + j], right = L[i * (m + 1) + j + 1];
                L[i * (m + 1) + j] = down > right ? down : right;
            }
        }
    int i = 0, j = 0, changes = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) { i++; j++; }
        else if (j < m && (i == n || L[i * (m + 1) + j + 1] >= L[(i + 1) * (m + 1) + j])) {
            std::cout << "  + " << (j + 1) << ": " << b[j] << "\n"; j++; changes++;
        } else {
            std::cout << "  - " << (i + 1) << ": " << a[i] << "\n"; i++; changes++;
        }
    }
    return changes;
}

#endif
