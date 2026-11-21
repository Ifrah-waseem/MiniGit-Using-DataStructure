
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <filesystem>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include "ds.h"
#include "algo.h"

using namespace std;
namespace fs = std::filesystem;

typedef DynArray<string> Lines;      
typedef AVLTree<Lines> Snapshot;     

const string ROOT = ".minigit";


struct Commit {
    string id, message, author, parent1, parent2;   // parent2 only for merge commits
    long long time;
    long size;                                      // total characters in snapshot
    int depth;                                      // distance from first commit
    Snapshot files;
    Commit() : time(0), size(0), depth(-1) {}
};

struct Branch { string name, id; };


static string shortId(const string& id) { return id.substr(0, 7); }

static string timeStr(long long t) {
    time_t tt = (time_t)t;
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", localtime(&tt));
    return buf;
}

static string hashText(const string& s) {            
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[20];
    snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

static bool readLines(const string& path, Lines& out) {
    ifstream f(path, ios::binary);                
    if (!f) return false;
    string data((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
        // UTF-16 file (PowerShell's  echo "x" > file  makes these): convert to UTF-8
        string u8;
        for (size_t i = 2; i + 1 < data.size(); i += 2) {
            unsigned c = (unsigned char)data[i] | ((unsigned)(unsigned char)data[i + 1] << 8);
            if (c < 0x80) u8 += (char)c;
            else if (c < 0x800) { u8 += (char)(0xC0 | (c >> 6)); u8 += (char)(0x80 | (c & 0x3F)); }
            else { u8 += (char)(0xE0 | (c >> 12)); u8 += (char)(0x80 | ((c >> 6) & 0x3F)); u8 += (char)(0x80 | (c & 0x3F)); }
        }
        data = u8;
    } else if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
               (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data.erase(0, 3);                          // remove UTF-8 BOM
    }
    string line;
    for (size_t i = 0; i < data.size(); i++) {     
        if (data[i] == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out.push_back(line);
            line.clear();
        } else line += data[i];
    }
    if (!line.empty()) {
        if (line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return true;
}

static void writeLines(const string& path, const Lines& lines) {
    fs::path p(path);
    if (p.has_parent_path()) { error_code ec; fs::create_directories(p.parent_path(), ec); }
    ofstream f(path);
    for (int i = 0; i < lines.size(); i++) f << lines[i] << "\n";
}

static string normalizePath(const string& p) {
    return fs::path(p).lexically_normal().generic_string();
}

static bool sameLines(Lines* a, Lines* b) {           
    if (!a && !b) return true;
    if (!a || !b) return false;
    return *a == *b;
}

static bool sameSnapshot(const Snapshot& a, const Snapshot& b) {
    if (a.size() != b.size()) return false;
    bool same = true;
    a.inorder([&](const string& n, Lines& ls) {
        if (!sameLines(&ls, b.find(n))) same = false;
    });
    return same;
}

static Snapshot without(const Snapshot& s, const string& name) {
    Snapshot r;
    s.inorder([&](const string& k, Lines& v) { if (k != name) r.insert(k, v); });
    return r;
}

static bool isTextFile(const fs::path& p) {            // used by "add <folder>"
    string e = p.extension().string();
    const char* ok[] = {".txt", ".cpp", ".h", ".c", ".hpp", ".md", ".py", ".java", ".csv", ".json"};
    for (const char* x : ok) if (e == x) return true;
    return false;
}

static bool validName(const string& n) {
    if (n.empty()) return false;
    for (char c : n)
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

static DynArray<string> tokenize(const string& line) {   
    DynArray<string> t;
    string cur;
    bool inq = false, has = false;
    char q = 0;
    for (char ch : line) {
        if (inq) { if (ch == q) inq = false; else cur += ch; }
        else if ((ch == '"' || ch == '\'') && cur.empty()) { inq = true; q = ch; has = true; }
        else if (isspace((unsigned char)ch)) {
            if (has || !cur.empty()) { t.push_back(cur); cur.clear(); has = false; }
        } else cur += ch;
    }
    if (has || !cur.empty()) t.push_back(cur);
    return t;
}

// comparison functions for quickSort
static bool byDate(Commit* const& a, Commit* const& b)   { return a->time < b->time; }
static bool bySize(Commit* const& a, Commit* const& b)   { return a->size < b->size; }
static bool byAuthor(Commit* const& a, Commit* const& b) { return a->author < b->author; }
static bool byDepthDesc(Commit* const& a, Commit* const& b) {
    if (a->depth != b->depth) return a->depth > b->depth;
    return a->time > b->time;
}

struct SizeItem {                                        
    long size; Commit* c;
    bool operator<(const SizeItem& o) const { return size < o.size; }
};

// ================= Repository =================
class Repository {
public:
    bool ready;
    string headBranch;
    DynArray<Commit*> commits;            // every commit (dynamic array)
    HashTable<Commit*> byId;              // commit id -> commit (hash table)
    DynArray<Branch> branches;            // branch = name + pointer to a commit
    Queue<string> staging;                // files waiting for the next commit
    string mergeHead, mergeName;          // set while a merge has conflicts

    Repository() : ready(false), headBranch("main") {}
    ~Repository() { for (int i = 0; i < commits.size(); i++) delete commits[i]; }

    // ---------- loading / saving ----------
    void load() {
        ready = fs::exists(ROOT) && fs::is_directory(ROOT);
        if (!ready) return;
        error_code ec;
        fs::create_directories(ROOT + "/commits", ec);
        fs::create_directories(ROOT + "/branches", ec);

        Lines tmp;
        if (readLines(ROOT + "/HEAD", tmp) && tmp.size() > 0) headBranch = tmp[0];

        for (auto& e : fs::directory_iterator(ROOT + "/commits")) loadCommit(e.path().string());
        for (int i = 0; i < commits.size(); i++) depthOf(commits[i]);

        for (auto& e : fs::directory_iterator(ROOT + "/branches")) {
            Lines l;
            readLines(e.path().string(), l);
            if (l.size() > 0) { Branch b; b.name = e.path().filename().string(); b.id = l[0]; branches.push_back(b); }
        }
        Lines idx;
        if (readLines(ROOT + "/index", idx))
            for (int i = 0; i < idx.size(); i++) staging.enqueue(idx[i]);

        Lines mh;
        if (readLines(ROOT + "/MERGE_HEAD", mh) && mh.size() >= 2) { mergeHead = mh[0]; mergeName = mh[1]; }
    }

    void loadCommit(const string& path) {
        Lines l;
        if (!readLines(path, l) || l.size() < 7) return;
        Commit* c = new Commit;
        c->id = l[0];
        c->parent1 = (l[1] == "-") ? "" : l[1];
        c->parent2 = (l[2] == "-") ? "" : l[2];
        c->time = atoll(l[3].c_str());
        c->author = l[4];
        c->message = l[5];
        int nfiles = atoi(l[6].c_str()), idx = 7;
        for (int f = 0; f < nfiles && idx + 1 < l.size(); f++) {
            string name = l[idx++];
            int nl = atoi(l[idx++].c_str());
            Lines ls;
            for (int k = 0; k < nl && idx < l.size(); k++) ls.push_back(l[idx++]);
            c->files.insert(name, ls);
        }
        computeSize(c);
        commits.push_back(c);
        byId.put(c->id, c);
    }

    void saveCommit(Commit* c) {
        Lines out;
        out.push_back(c->id);
        out.push_back(c->parent1.empty() ? "-" : c->parent1);
        out.push_back(c->parent2.empty() ? "-" : c->parent2);
        out.push_back(to_string(c->time));
        out.push_back(c->author);
        out.push_back(c->message);
        out.push_back(to_string(c->files.size()));
        c->files.inorder([&](const string& name, Lines& ls) {
            out.push_back(name);
            out.push_back(to_string(ls.size()));
            for (int i = 0; i < ls.size(); i++) out.push_back(ls[i]);
        });
        writeLines(ROOT + "/commits/" + c->id, out);
    }

    void saveState() {                    
        if (!ready) return;
        Lines h; h.push_back(headBranch);
        writeLines(ROOT + "/HEAD", h);

        error_code ec;
        fs::remove_all(ROOT + "/branches", ec);
        fs::create_directories(ROOT + "/branches", ec);
        for (int i = 0; i < branches.size(); i++) {
            Lines b; b.push_back(branches[i].id);
            writeLines(ROOT + "/branches/" + branches[i].name, b);
        }
        Lines idx;
        staging.each([&](const string& f) { idx.push_back(f); });
        writeLines(ROOT + "/index", idx);

        if (mergeHead.empty()) fs::remove(ROOT + "/MERGE_HEAD", ec);
        else { Lines m; m.push_back(mergeHead); m.push_back(mergeName); writeLines(ROOT + "/MERGE_HEAD", m); }
    }

    // ---------- lookups ----------
    Commit* getCommit(const string& id) {
        if (id.empty()) return nullptr;
        Commit** p = byId.get(id);
        return p ? *p : nullptr;
    }
    Branch* findBranch(const string& name) {
        for (int i = 0; i < branches.size(); i++) if (branches[i].name == name) return &branches[i];
        return nullptr;
    }
    void setBranch(const string& name, const string& id) {
        Branch* b = findBranch(name);
        if (b) b->id = id;
        else { Branch nb; nb.name = name; nb.id = id; branches.push_back(nb); }
    }
    void removeBranch(const string& name) {
        DynArray<Branch> keep;
        for (int i = 0; i < branches.size(); i++) if (branches[i].name != name) keep.push_back(branches[i]);
        branches = keep;
    }
    Commit* headCommit() {
        Branch* b = findBranch(headBranch);
        return b ? getCommit(b->id) : nullptr;
    }
    
    Commit* resolve(const string& t) {
        if (t == "HEAD") return headCommit();
        Branch* b = findBranch(t);
        if (b) return getCommit(b->id);
        Commit* c = getCommit(t);
        if (c) return c;
        if (t.size() >= 4) {
            Commit* found = nullptr; int cnt = 0;
            for (int i = 0; i < commits.size(); i++)
                if (commits[i]->id.compare(0, t.size(), t) == 0) { found = commits[i]; cnt++; }
            if (cnt == 1) return found;
        }
        return nullptr;
    }
    
    int depthOf(Commit* c) {
        if (c->depth >= 0) return c->depth;
        int d = 0;
        Commit* p1 = getCommit(c->parent1);
        Commit* p2 = getCommit(c->parent2);
        if (p1) d = depthOf(p1) + 1;
        if (p2) { int x = depthOf(p2) + 1; if (x > d) d = x; }
        c->depth = d;
        return d;
    }
    static void computeSize(Commit* c) {
        long total = 0;
        c->files.inorder([&](const string&, Lines& ls) {
            for (int i = 0; i < ls.size(); i++) total += ls[i].size() + 1;
        });
        c->size = total;
    }
    string labelsFor(Commit* c) {
        string s;
        for (int i = 0; i < branches.size(); i++)
            if (branches[i].id == c->id) {
                if (!s.empty()) s += ", ";
                s += (branches[i].name == headBranch) ? "HEAD -> " + branches[i].name : branches[i].name;
            }
        return s.empty() ? "" : " (" + s + ")";
    }
    void stage(const string& f) { if (!staging.contains(f)) staging.enqueue(f); }

    bool isDirty() {                       // staged or modified tracked files?
        if (!staging.empty()) return true;
        Commit* h = headCommit();
        if (!h) return false;
        bool dirty = false;
        h->files.inorder([&](const string& n, Lines& ls) {
            Lines w;
            if (!readLines(n, w) || !(w == ls)) dirty = true;
        });
        return dirty;
    }


    void restoreWorkingDir(Commit* target, Commit* from) {
        if (from)
            from->files.inorder([&](const string& n, Lines&) {
                if (!target || !target->files.find(n)) { error_code ec; fs::remove(n, ec); }
            });
        if (target)
            target->files.inorder([&](const string& n, Lines& ls) { writeLines(n, ls); });
    }

    
    void init() {
        if (ready) { cout << "Repository already initialised.\n"; return; }
        error_code ec;
        fs::create_directories(ROOT + "/commits", ec);
        fs::create_directories(ROOT + "/branches", ec);
        ready = true;
        headBranch = "main";
        cout << "Initialised empty Mini Git repository in " << ROOT << "/\n";
    }

    void addFolder(const fs::path& dir) {
        for (auto& e : fs::directory_iterator(dir)) {
            if (e.path().filename() == ROOT) continue;
            if (e.is_directory()) addFolder(e.path());
            else if (e.is_regular_file() && isTextFile(e.path())) {
                stage(normalizePath(e.path().string()));
                cout << "  staged " << normalizePath(e.path().string()) << "\n";
            }
        }
    }

    void add(const DynArray<string>& args) {
        if (args.size() < 2) { cout << "Usage: add <file|folder> ...\n"; return; }
        Commit* h = headCommit();
        for (int i = 1; i < args.size(); i++) {
            string p = normalizePath(args[i]);
            if (fs::is_directory(p)) addFolder(p);
            else if (fs::exists(p)) { stage(p); cout << "  staged " << p << "\n"; }
            else if (h && h->files.find(p)) { stage(p); cout << "  staged deletion of " << p << "\n"; }
            else cout << "  file not found: " << p << "\n";
        }
    }

    Commit* makeCommit(const string& msg, const string& p1, const string& p2, const Snapshot& snap) {
        Commit* c = new Commit;
        const char* user = getenv("USER");
        if (!user) user = getenv("USERNAME");
        c->author = user ? user : "student";
        c->message = msg;
        c->time = (long long)time(nullptr);
        c->parent1 = p1;
        c->parent2 = p2;
        c->files = snap;
       
        string content = msg + "|" + to_string(c->time) + "|" + p1 + "|" + p2 + "|";
        snap.inorder([&](const string& name, Lines& ls) {
            content += name + "\n";
            for (int i = 0; i < ls.size(); i++) content += ls[i] + "\n";
        });
        c->id = hashText(content);
        Commit* existing = getCommit(c->id);
        if (existing) { delete c; return existing; }
        computeSize(c);
        commits.push_back(c);
        byId.put(c->id, c);
        depthOf(c);
        saveCommit(c);
        return c;
    }

    void commitStaged(string msg) {
        bool merging = !mergeHead.empty();
        if (staging.empty() && !merging) { cout << "Nothing to commit (use 'add' first).\n"; return; }

        bool bad = false;                              
        staging.each([&](const string& f) {
            Lines ls;
            if (readLines(f, ls))
                for (int i = 0; i < ls.size(); i++)
                    if (ls[i].compare(0, 7, "<<<<<<<") == 0) {
                        cout << "Unresolved conflict markers in " << f << " - fix it and commit again.\n";
                        bad = true; break;
                    }
        });
        if (bad) return;

        string curId = (headCommit() ? headCommit()->id : "");
        Commit* parent = headCommit();
        Snapshot snap;
        if (parent) snap = parent->files;
        while (!staging.empty()) {                     
            string f = staging.dequeue();
            Lines ls;
            if (readLines(f, ls)) snap.insert(f, ls);
            else snap = without(snap, f);              
        }
        if (parent && !merging && sameSnapshot(snap, parent->files)) {
            cout << "No changes since the last commit.\n"; return;
        }
        if (msg.empty() && merging) msg = "Merge branch '" + mergeName + "'";

        Commit* c = makeCommit(msg, curId, mergeHead, snap);
        setBranch(headBranch, c->id);
        mergeHead = ""; mergeName = "";
        cout << "[" << headBranch << " " << shortId(c->id) << "] " << msg
             << " (" << snap.size() << " files)\n";
    }

    void status() {
        cout << "On branch " << headBranch << "\n";
        if (!mergeHead.empty()) cout << "Merge in progress with '" << mergeName << "'\n";
        cout << "Staged for commit:\n";
        if (staging.empty()) cout << "  (nothing)\n";
        staging.each([&](const string& f) { cout << "  " << f << "\n"; });
        cout << "Modified / deleted but not staged:\n";
        Commit* h = headCommit();
        int cnt = 0;
        if (h)
            h->files.inorder([&](const string& n, Lines& ls) {
                if (staging.contains(n)) return;
                Lines w;
                if (!readLines(n, w)) { cout << "  deleted:  " << n << "\n"; cnt++; }
                else if (!(w == ls)) { cout << "  modified: " << n << "\n"; cnt++; }
            });
        if (cnt == 0) cout << "  (nothing)\n";
    }

    void log() {
        Commit* c = headCommit();
        if (!c) { cout << "No commits yet.\n"; return; }
        LinkedList<Commit*> chain;                    
        while (c) {
            chain.push_back(c);
            c = getCommit(c->parent1);
        }
        for (auto* n = chain.head; n; n = n->next) {
            Commit* x = n->val;
            cout << "commit " << x->id << labelsFor(x) << "\n";
            if (!x->parent2.empty()) cout << "Merge: " << shortId(x->parent1) << " " << shortId(x->parent2) << "\n";
            cout << "Author: " << x->author << "\nDate:   " << timeStr(x->time) << "\n\n    " << x->message << "\n\n";
        }
    }

    // ---------- branches ----------
    void branch(const DynArray<string>& args) {
        if (args.size() < 2) {
            if (branches.size() == 0) cout << "No branches yet (make a commit first).\n";
            for (int i = 0; i < branches.size(); i++)
                cout << (branches[i].name == headBranch ? "* " : "  ") << branches[i].name
                     << "  " << shortId(branches[i].id) << "\n";
            return;
        }
        string name = args[1];
        Commit* h = headCommit();
        if (!validName(name)) { cout << "Invalid branch name.\n"; return; }
        if (!h) { cout << "Make a commit before creating branches.\n"; return; }
        if (findBranch(name)) { cout << "Branch already exists.\n"; return; }
        setBranch(name, h->id);
        cout << "Created branch " << name << " at " << shortId(h->id) << "\n";
    }

    void checkout(const DynArray<string>& args) {
        if (args.size() < 2) { cout << "Usage: checkout <branch>   or   checkout -b <new-branch>\n"; return; }
        bool create = (args[1] == "-b");
        string name = create ? (args.size() > 2 ? args[2] : "") : args[1];
        if (name.empty()) { cout << "Branch name missing.\n"; return; }

        if (create) {
            Commit* h = headCommit();
            if (!validName(name)) { cout << "Invalid branch name.\n"; return; }
            if (!h) { cout << "Make a commit first.\n"; return; }
            if (findBranch(name)) { cout << "Branch already exists.\n"; return; }
            setBranch(name, h->id);
            headBranch = name;
            cout << "Switched to new branch " << name << "\n";
            return;
        }
        Branch* b = findBranch(name);
        if (!b) { cout << "No such branch: " << name << "\n"; return; }
        if (name == headBranch) { cout << "Already on " << name << "\n"; return; }
        if (!mergeHead.empty() || isDirty()) {
            cout << "You have uncommitted changes. Commit them before switching.\n"; return;
        }
        restoreWorkingDir(getCommit(b->id), headCommit());
        headBranch = name;
        cout << "Switched to branch " << name << "\n";
    }


    Commit* commonAncestor(Commit* a, Commit* b) {
        HashTable<int> seenA, seenB;
        Queue<Commit*> q;
        seenA.put(a->id, 1);
        q.enqueue(a);
        while (!q.empty()) {
            Commit* c = q.dequeue();
            for (int i = 0; i < 2; i++) {
                Commit* p = getCommit(i == 0 ? c->parent1 : c->parent2);
                if (p && !seenA.get(p->id)) { seenA.put(p->id, 1); q.enqueue(p); }
            }
        }
        seenB.put(b->id, 1);
        q.enqueue(b);
        while (!q.empty()) {
            Commit* c = q.dequeue();
            if (seenA.get(c->id)) { q.clear(); return c; }
            for (int i = 0; i < 2; i++) {
                Commit* p = getCommit(i == 0 ? c->parent1 : c->parent2);
                if (p && !seenB.get(p->id)) { seenB.put(p->id, 1); q.enqueue(p); }
            }
        }
        return nullptr;
    }

    void merge(const DynArray<string>& args) {
        if (args.size() < 2) { cout << "Usage: merge <branch>\n"; return; }
        string bname = args[1];
        Branch* b = findBranch(bname);
        if (!b) { cout << "No such branch: " << bname << "\n"; return; }
        if (bname == headBranch) { cout << "Cannot merge a branch into itself.\n"; return; }
        if (!mergeHead.empty()) { cout << "Finish the current merge first (fix conflicts, then commit).\n"; return; }
        if (isDirty()) { cout << "Commit your changes before merging.\n"; return; }
        Commit* ours = headCommit();
        Commit* theirs = getCommit(b->id);
        if (!ours || !theirs) { cout << "Nothing to merge.\n"; return; }

        Commit* base = commonAncestor(ours, theirs);
        if (base == theirs) { cout << "Already up to date.\n"; return; }
        if (base == ours) {                            // fast-forward
            setBranch(headBranch, theirs->id);
            restoreWorkingDir(theirs, ours);
            cout << "Fast-forward to " << shortId(theirs->id) << "\n";
            return;
        }
     
        Snapshot emptySnap;
        const Snapshot* baseSnap = base ? &base->files : &emptySnap;
        cout << "Merging '" << bname << "' into '" << headBranch << "' (common ancestor "
             << (base ? shortId(base->id) : "none") << ")\n";

        AVLTree<int> names;                            // union of all file names
        baseSnap->inorder([&](const string& k, Lines&) { names.insert(k, 1); });
        ours->files.inorder([&](const string& k, Lines&) { names.insert(k, 1); });
        theirs->files.inorder([&](const string& k, Lines&) { names.insert(k, 1); });

        int conflicts = 0;
        names.inorder([&](const string& n, int&) {
            Lines* bl = baseSnap->find(n);
            Lines* ol = ours->files.find(n);
            Lines* tl = theirs->files.find(n);
            Lines* result = nullptr;
            bool conflict = false;
            if (sameLines(ol, tl)) result = ol;        // both sides same
            else if (sameLines(bl, ol)) result = tl;   // only theirs changed
            else if (sameLines(bl, tl)) result = ol;   // only ours changed
            else conflict = true;                      // both changed differently

            if (conflict) {
                Lines m;
                m.push_back("<<<<<<< " + headBranch);
                if (ol) for (int i = 0; i < ol->size(); i++) m.push_back((*ol)[i]);
                m.push_back("=======");
                if (tl) for (int i = 0; i < tl->size(); i++) m.push_back((*tl)[i]);
                m.push_back(">>>>>>> " + bname);
                writeLines(n, m);
                stage(n);
                conflicts++;
                cout << "  CONFLICT: " << n << "\n";
            } else if (!sameLines(result, ol)) {
                if (result) writeLines(n, *result);
                else { error_code ec; fs::remove(n, ec); }
                stage(n);
                cout << "  merged:   " << n << "\n";
            }
        });
        mergeHead = theirs->id;
        mergeName = bname;
        if (conflicts == 0) commitStaged("");
        else cout << "Automatic merge failed. Edit the files above (remove the <<<<<<< ======= >>>>>>> lines),\n"
                  << "then run: commit\n";
    }

    // ---------- diff ----------
    void printFileDiff(const string& name, Lines* a, Lines* b) {
        if (sameLines(a, b)) return;
        Lines empty;
        cout << "--- " << name << (a ? "" : "  (new file)") << (b ? "" : "  (deleted)") << "\n";
        lcsDiff(a ? *a : empty, b ? *b : empty);
    }

    void diff(const DynArray<string>& args) {
        Commit* h = headCommit();
        if (args.size() <= 2) {                       
            if (!h) { cout << "No commits yet.\n"; return; }
            if (args.size() == 2) {
                string f = normalizePath(args[1]);
                Lines w; Lines* wp = readLines(f, w) ? &w : nullptr;
                if (!wp && !h->files.find(f)) { cout << "Unknown file: " << f << "\n"; return; }
                if (sameLines(h->files.find(f), wp)) cout << "No differences.\n";
                else printFileDiff(f, h->files.find(f), wp);
                return;
            }
            bool any = false;
            h->files.inorder([&](const string& n, Lines& ls) {
                Lines w; Lines* wp = readLines(n, w) ? &w : nullptr;
                if (!sameLines(&ls, wp)) { printFileDiff(n, &ls, wp); any = true; }
            });
            if (!any) cout << "No differences.\n";
            return;
        }
        Commit* c1 = resolve(args[1]);
        Commit* c2 = resolve(args[2]);
        if (!c1 || !c2) { cout << "Unknown commit or branch.\n"; return; }
        AVLTree<int> names;
        c1->files.inorder([&](const string& k, Lines&) { names.insert(k, 1); });
        c2->files.inorder([&](const string& k, Lines&) { names.insert(k, 1); });
        bool any = false;
        names.inorder([&](const string& n, int&) {
            if (args.size() > 3 && n != normalizePath(args[3])) return;
            if (!sameLines(c1->files.find(n), c2->files.find(n))) {
                printFileDiff(n, c1->files.find(n), c2->files.find(n)); any = true;
            }
        });
        if (!any) cout << "No differences.\n";
    }

    
    DynArray<Commit*> sortedCommits(bool (*less)(Commit* const&, Commit* const&)) {
        DynArray<Commit*> a = commits;
        quickSort(a, 0, a.size() - 1, less);
        return a;
    }

    void search(const DynArray<string>& args) {
        if (args.size() < 2) { cout << "Usage: search <text> [--kmp|--rk|--naive]\n"; return; }
        string pat = args[1], algo = "kmp";
        if (args.size() > 2) algo = args[2].substr(2);
        int (*finder)(const string&, const string&, int) = kmpFind;
        if (algo == "rk") finder = rabinKarpFind;
        else if (algo == "naive") finder = naiveFind;
        cout << "Searching '" << pat << "' in all commits using " << algo << "...\n";
        int hits = 0;
        DynArray<Commit*> sorted = sortedCommits(byDate);
        for (int i = 0; i < sorted.size(); i++) {
            Commit* c = sorted[i];
            c->files.inorder([&](const string& n, Lines& ls) {
                for (int l = 0; l < ls.size(); l++)
                    if (finder(ls[l], pat, 0) >= 0) {
                        cout << "  " << shortId(c->id) << "  " << n << ":" << (l + 1) << ": " << ls[l] << "\n";
                        hits++;
                    }
            });
        }
        cout << hits << " match(es).\n";
    }

    // ---------- listing, sorting, graph ----------
    void sortCommits(const DynArray<string>& args) {
        string key = args.size() > 1 ? args[1] : "date";
        DynArray<Commit*> a;
        if (key == "date") a = sortedCommits(byDate);
        else if (key == "size") a = sortedCommits(bySize);
        else if (key == "author") a = sortedCommits(byAuthor);
        else { cout << "Usage: sort <date|size|author>\n"; return; }
        cout << "Commits sorted by " << key << ":\n";
        for (int i = 0; i < a.size(); i++)
            cout << "  " << shortId(a[i]->id) << "  " << timeStr(a[i]->time) << "  " << a[i]->author
                 << "  " << a[i]->size << " chars  " << a[i]->message << "\n";
    }

    void top(const DynArray<string>& args) {           // largest commits using a heap
        int n = args.size() > 1 ? atoi(args[1].c_str()) : 3;
        MaxHeap<SizeItem> heap;
        for (int i = 0; i < commits.size(); i++) { SizeItem it; it.size = commits[i]->size; it.c = commits[i]; heap.push(it); }
        cout << "Largest commits:\n";
        for (int i = 0; i < n && !heap.empty(); i++) {
            SizeItem it = heap.pop();
            cout << "  " << shortId(it.c->id) << "  " << it.size << " chars  " << it.c->message << "\n";
        }
    }

    void graph() {
        if (commits.size() == 0) { cout << "No commits yet.\n"; return; }
        DynArray<Commit*> a = sortedCommits(byDepthDesc);   // newest first
        for (int i = 0; i < a.size(); i++) {
            Commit* c = a[i];
            cout << "* " << shortId(c->id) << labelsFor(c) << "  " << c->message << "\n";
            if (!c->parent2.empty())
                cout << "|\\  merge of " << shortId(c->parent1) << " and " << shortId(c->parent2) << "\n";
            if (i + 1 < a.size()) cout << "|\n";
        }
    }
};


static void help() {
    cout << "Mini Git commands:\n"
            "  init                       create a repository\n"
            "  add <file|folder> ...      stage files for the next commit\n"
            "  commit -m \"message\"        save a snapshot of the staged files\n"
            "  status                     show branch, staged and changed files\n"
            "  log                        show commit history of the current branch\n"
            "  diff [file]                working folder vs last commit\n"
            "  diff <c1> <c2> [file]      compare two commits/branches\n"
            "  branch [name]              list branches or create one\n"
            "  checkout <branch>          switch branch   (checkout -b <name> creates it)\n"
            "  merge <branch>             merge a branch into the current one\n"
            "  search <text> [--kmp|--rk|--naive]   search text in every commit\n"
            "  sort <date|size|author>    list commits sorted\n"
            "  top [n]                    n largest commits (heap)\n"
            "  graph                      ASCII commit graph\n"
            "  help, exit\n";
}

static bool runCommand(Repository& repo, const DynArray<string>& a) {
    if (a.size() == 0) return true;
    const string& cmd = a[0];
    if (cmd == "exit" || cmd == "quit") return false;
    if (cmd == "help") { help(); return true; }
    if (cmd == "init") { repo.init(); return true; }
    if (!repo.ready) { cout << "Not a Mini Git repository. Run: init\n"; return true; }

    if (cmd == "add") repo.add(a);
    else if (cmd == "commit") {
        string msg;
        for (int i = 1; i < a.size(); i++)
            if (a[i] == "-m") {
                for (int j = i + 1; j < a.size(); j++) msg += (msg.empty() ? "" : " ") + a[j];
                break;
            }
        if (msg.empty() && repo.mergeHead.empty()) cout << "Usage: commit -m \"message\"\n";
        else repo.commitStaged(msg);
    }
    else if (cmd == "status") repo.status();
    else if (cmd == "log") repo.log();
    else if (cmd == "diff") repo.diff(a);
    else if (cmd == "branch") repo.branch(a);
    else if (cmd == "checkout") repo.checkout(a);
    else if (cmd == "merge") repo.merge(a);
    else if (cmd == "search") repo.search(a);
    else if (cmd == "sort") repo.sortCommits(a);
    else if (cmd == "top") repo.top(a);
    else if (cmd == "graph") repo.graph();
    else cout << "Unknown command '" << cmd << "'. Type 'help'.\n";
    return true;
}

int main(int argc, char* argv[]) {
    Repository repo;
    repo.load();

    if (argc > 1) {                                   
        DynArray<string> args;
        for (int i = 1; i < argc; i++) args.push_back(argv[i]);
        runCommand(repo, args);
        repo.saveState();
        return 0;
    }
    cout << "Mini Git - type 'help' for commands, 'exit' to quit.\n";
    string line;
    while (true) {                                     
        cout << "minigit> ";
        if (!getline(cin, line)) break;
        if (!runCommand(repo, tokenize(line))) break;
        repo.saveState();
    }
    repo.saveState();
    return 0;
}