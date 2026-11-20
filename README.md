# Mini Git – A Console Version Control System (C++)

Data Structures project, NUCES Karachi. All core logic uses our own data structures (no STL containers).

## Build and run
```
make                 (or: g++ -std=c++17 -O2 -o minigit main.cpp)
./minigit            interactive shell (Windows: minigit.exe)
./minigit status     or run a single command
```
Needs g++ 8+ with C++17 (`<filesystem>`). In Dev-C++ set the compiler option `-std=c++17`.
If g++ 8 complains about filesystem, add `-lstdc++fs`.

## Quick demo
```
init
add notes.txt
commit -m "first version"
branch dev
checkout dev
(edit notes.txt)
add notes.txt
commit -m "dev change"
diff main dev
checkout main
merge dev
graph
search hello --rk
```

## Commands
| Command | What it does |
|---|---|
| `init` | create the hidden `.minigit` folder |
| `add <file or folder>` | stage files (a folder is scanned recursively for text files) |
| `commit -m "msg"` | save a snapshot of the staged files |
| `status` / `log` | current state, commit history |
| `diff [file]` / `diff c1 c2 [file]` | LCS line diff |
| `branch [name]` / `checkout [-b] name` | list/create branches, switch branch |
| `merge <branch>` | fast-forward or three-way merge with conflict markers |
| `search <text> [--kmp\|--rk\|--naive]` | search all commits |
| `sort <date\|size\|author>` / `top [n]` | sorted commit list / largest commits |
| `graph` | ASCII commit graph |

A commit id can be a branch name, `HEAD`, the full id or a unique prefix (4+ chars).

## Where each course topic is used
| Topic | Used for |
|---|---|
| Classes, pointers | `Repository`, `Commit`, `Branch`, all templates |
| Dynamic array | `DynArray`: lines of every file, list of commits |
| Linked list | `LinkedList`: log chain |
| Queue | `Queue`: staging area (files waiting for commit) and BFS |
| Recursion | `addFolder` (folders), `depthOf` (graph depth), AVL tree operations |
| Searching / sorting | prefix search of commit ids, `quickSort` for `sort`, `search`|
| Trees (BST / AVL) | `AVLTree`: balanced file index of every commit |
| Heap / hashing | `MaxHeap` for `top`; `HashTable` maps commit id -> commit |
| Graph | commits form a DAG; `commonAncestor` does BFS |
| String algorithms | `naiveFind`, `kmpFind`, `rabinKarpFind` (for `search`), `lcsDiff` (for `diff`) |

## Files
- `ds.h`    all data structures
- `algo.h`  string search and LCS diff
- `main.cpp` repository logic and command line

## How it works (short)
- Each commit is one file in `.minigit/commits/` holding id, parents, message and a full snapshot of its files.
- The id is a 64-bit FNV hash of the commit content.
- A branch is a file in `.minigit/branches/` containing a commit id.
- `merge` finds the common ancestor with BFS, then compares base / ours / theirs per file.
  If both sides changed a file differently it is written with `<<<<<<<`, `=======`, `>>>>>>>` markers.
  Fix the file, `commit`, and the merge finishes (commit is refused while markers remain).

## Limits (kept simple on purpose)
- Text files only; merge conflicts are per file, not per line.
- Every commit stores a full snapshot (no delta compression).
- Commit messages are one line.
