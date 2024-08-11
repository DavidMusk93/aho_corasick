#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#ifndef NDEBUG
#include <stdio.h>
#include <iostream>
#include <sstream>
using namespace std::literals;
#endif

namespace ac {

struct alignas(64) Node {
    std::vector<int> next;
    int input_ref{-1};
    int fail_ref{0};
    int prev_ref;
    int level;
    int edge;

    Node(int prev_ref, int level, int edge) {
        next.resize(256, -1);
        this->prev_ref = prev_ref;
        this->level = level;
        this->edge = edge;
    }
#ifndef NDEBUG
    std::ostream& Print(std::ostream& os) const {
        char buf[512];
        std::string e = edge ? std::string(1, (char)edge) : "\0"s;
        std::ostringstream ss;
        int n{};
        for (int i = 0; i < 256; ++i) {
            if (next[i] != -1) {
                if (n++) ss << ',';
                ss << '"' << (char)i << '(' << next[i] << ")\""sv;
            }
        }
        size_t len = snprintf(
            buf, sizeof(buf),
            R"({"edge":"%s","level":%d,"prev_ref":%d,"input_ref":%d,"fail_ref":%d,"next":[%s]})",
            e.c_str(), level, prev_ref, input_ref, fail_ref, ss.str().c_str());
        return os << std::string_view{buf, len};
    }
#endif
};

class Result {
    std::vector<int> refs_;

   public:
    void Add(int ref) {
        if (ref != -1) refs_.push_back(ref);
    }

    void Merge(const Result& rhs) {
        if (rhs.refs_.empty()) return;
        refs_.reserve(refs_.size() + rhs.refs_.size());
        refs_.insert(refs_.end(), rhs.refs_.begin(), rhs.refs_.end());
    }

    auto& refs() {
        return refs_;
    }
};

class Refs {
    int fail_;
    struct {
        int input_ : 31;
        int match_ : 1;
    };

   public:
    Refs(int fail, int input) : fail_{fail}, input_{input}, match_{0} {}

    auto fail() const {
        return fail_;
    }

    auto input() const {
        return input_;
    }

    void set_match(bool x) {
        match_ = x;
    }

    bool match() const {
        return match_;
    }
};

struct Context {
    using buf_t = std::string;

    static void prepare_buf(buf_t& /* buf */, int /* n_nodes */) {}
    static void* make_context() {
        return {};
    }
    static void reclaim_context(void* /* context */) {}
};

class Node2 : public Refs, public Context {
    using Refs::Refs;
    std::vector<int> next_;

   public:
    auto State(uint8_t x, const char* /* buf */) const {
        return next_[x];
    }

    static Node2 of(Node& n, buf_t&, void*) {
        Node2 r{n.fail_ref, n.input_ref};
        r.next_ = std::move(n.next);
        return r;
    }
};

class Node3 : public Refs, public Context {
    using Refs::Refs;
    int* next_;

   public:
    int State(uint8_t x, const char*) const {
        return next_[x];
    }

    static void prepare_buf(buf_t& buf, int n_nodes) {
        buf.reserve(n_nodes * 256 * sizeof(int));
    }

    static Node3 of(Node& n, buf_t& buf, void*) {
        Node3 r{n.fail_ref, n.input_ref};
        r.next_ = (int*)(buf.data() + buf.size());
        buf.insert(buf.end(), (char*)n.next.data(),
                   (char*)(n.next.data() + n.next.size()));
        return r;
    }
};

class Node4 : public Refs, public Context {
    struct context_t {
        std::vector<int> refs;
        std::vector<char> edges;
    };
    using Refs::Refs;
    int off_;
    int len_;

   public:
    int State(uint8_t x, const char* buf) const {
        if (!len_) return -1;
        auto p_refs = (const int*)(buf + off_);
        if (len_ == 256) return p_refs[x];
        auto p_edges = buf + off_ + len_ * 4;
        if (len_ == 1) return p_edges[0] == x ? p_refs[0] : -1;
        auto t = memchr(p_edges, x, len_);
        if (!t) return -1;
        return p_refs[(const char*)t - p_edges];
    }

    static void prepare_buf(buf_t& buf, int n_nodes) {
        buf.reserve(n_nodes * 5 * sizeof(int));
    }

    static void* make_context() {
        auto r = new context_t;
        r->refs.reserve(256);
        r->edges.reserve(256);
        return r;
    }

    static void reclaim_context(void* context) {
        delete static_cast<context_t*>(context);
    }

    static Node4 of(Node& n, buf_t& buf, void* context) {
        Node4 r{n.fail_ref, n.input_ref};
        auto& ctx = *static_cast<context_t*>(context);
        ctx.refs.clear();
        ctx.edges.clear();

        for (int i = 0; i < 256; ++i) {
            if (n.next[i] != -1) {
                ctx.refs.push_back(n.next[i]);
                ctx.edges.push_back((char)i);
            }
        };

        const int len = ctx.refs.size();
        r.off_ = buf.size();
        r.len_ = len;
        if (len) {
            buf.insert(buf.end(), (char*)ctx.refs.data(), (char*)(ctx.refs.data() + len));
            buf.insert(buf.end(), ctx.edges.data(), (ctx.edges.data() + len));
            if (len & 3) {
                std::string t('\0', 4 - (len & 3));
                buf.insert(buf.end(), t.begin(), t.end());
            }
        }
        return r;
    }
};

class Node5 : public Refs, public Context {
    struct map_t {
        uint64_t bitmap[4];
        char counts[4];
        int refs[];
    };
    struct context_t {
        std::vector<int> refs;
        map_t map;
    };
    using Refs::Refs;
    int total_;
    int off_;

   public:
    int State(uint8_t x, const char* buf) const {
        if (!total_) return -1;
        auto map = (const map_t*)(buf + off_);
        if (total_ == 256) return map->refs[x];
        const int n = x / 64;
        const auto mask = 1UL << (x & 63);
        if (map->bitmap[n] & mask) {
            if (total_ == 1) return map->refs[0];
            int count = 0;
            for (int i = 0; i < n; ++i) count += map->counts[i];
            const auto x = map->bitmap[n] & (mask - 1);
            if (x) count += __builtin_popcountl(x);
            return map->refs[count];
        }
        return -1;
    }

    static void prepare_buf(buf_t& buf, int n_nodes) {
        buf.reserve(n_nodes * sizeof(map_t));
    }

    static void* make_context() {
        auto r = new context_t;
        r->refs.reserve(256);
        return r;
    }

    static void reclaim_context(void* context) {
        delete static_cast<context_t*>(context);
    }

    static Node5 of(Node& n, buf_t& buf, void* context) {
        Node5 r{n.fail_ref, n.input_ref};
        auto& ctx = *static_cast<context_t*>(context);
        ctx.refs.clear();
        auto& map = ctx.map;
        memset(map.bitmap, 0, sizeof(map.bitmap));
        for (int i = 0; i < 256; ++i) {
            if (n.next[i] != -1) {
                map.bitmap[i / 64] |= 1UL << (i & 63);
                ctx.refs.push_back(n.next[i]);
            }
        };
        r.off_ = buf.size();
        r.total_ = ctx.refs.size();
        if (r.total_) {
            for (int i = 0; i < 4; ++i)
                map.counts[i] = __builtin_popcountl(map.bitmap[i]);
            buf.insert(buf.end(), (char*)&map, (char*)map.refs);
            buf.insert(buf.end(), (char*)ctx.refs.data(),
                       (char*)(ctx.refs.data() + r.total_));
        }
        return r;
    }
};

template <typename FINAL_NODE = Node2>
class Trie {
    std::vector<FINAL_NODE> finals_;
    typename FINAL_NODE::buf_t buf_;
    std::vector<std::string> inputs_;
    std::vector<Result> results_;
    std::vector<Node> nodes_;

    template <typename F>
    bool match(std::string_view s, F&& f) const {
        int state = 0;
        const int n = s.size();
        auto p = (const uint8_t*)s.data();
        auto buf = buf_.data();

        for (int i = 0; i < n; ++i) {
            for (;;) {
                auto& n = finals_[state];
                int t = n.State(p[i], buf);
                if (t == -1) {
                    state = n.fail();
                } else {
                    state = t;
                    break;
                }
            }
            if (state && finals_[state].match() && f(state) != 0) return true;
        }
        return false;
    }

   public:
    Trie(size_t size_hint = 16) {
        inputs_.reserve(size_hint);
        nodes_.reserve(size_hint * 4);
        nodes_.push_back(Node(0, 0, 0));
    }

    void Add(std::string_view pattern) {
        int i{};
        int state = 0;
        const int n = pattern.size();
        auto p = (const uint8_t*)pattern.data();

        for (; i < n; ++i) {
            int t = nodes_[state].next[p[i]];
            if (t == -1) break;
            state = t;
        }

        for (; i < n; ++i) {
            int t = nodes_.size();
            nodes_.push_back(Node(state, nodes_[state].level + 1, p[i]));
            nodes_[state].next[p[i]] = t;
            state = t;
        }

        inputs_.push_back(std::string{pattern});
        nodes_[state].input_ref = inputs_.size() - 1;
    }

    void Build() {
        for (auto& x : nodes_[0].next) {
            if (x == -1) x = 0;
        }

        const int n = nodes_.size();
        std::vector<Node*> pointers;
        pointers.reserve(n);
        for (auto& x : nodes_) {
            pointers.push_back(&x);
        }
        std::sort(pointers.begin(), pointers.end(),
                  [](auto l, auto r) { return l->level < r->level; });

        results_.resize(n);
        for (int i = 0; i < n; ++i) {
            results_[i].Add(nodes_[i].input_ref);
        }

        int i = 1;
        for (; i < n; ++i) {
            if (pointers[i]->level != 1) break;
        }

        for (; i < n; ++i) {
            auto& self = *pointers[i];
            int fail = nodes_[self.prev_ref].fail_ref;
            for (;;) {
                int t = nodes_[fail].next[self.edge];
                if (t != -1) {
                    results_[&self - &nodes_[0]].Merge(results_[t]);
                    self.fail_ref = t;
                    break;
                }
                fail = nodes_[fail].fail_ref;
            }
        }
#ifndef NDEBUG
        std::cout << '[';
        for (i = 1; i < n; ++i) {
            if (i != 1) std::cout << ',';
            pointers[i]->Print(std::cout) << std::endl;
        }
        std::cout << ']' << std::endl;
#endif

        FINAL_NODE::prepare_buf(buf_, nodes_.size());
        finals_.reserve(nodes_.size());
        auto context = FINAL_NODE::make_context();
        for (auto& x : nodes_) {
            finals_.push_back(FINAL_NODE::of(x, buf_, context));
        }
        FINAL_NODE::reclaim_context(context);
        nodes_ = {};
        buf_.shrink_to_fit();

        for (int i = 0; i < n; ++i) {
            finals_[i].set_match(!results_[i].refs().empty());
        }
    }

    bool Match(std::string_view s) const {
        return match(s, [](auto) { return 1; });
    }

    bool Match(std::string_view s, std::vector<std::string_view>& matches) const {
        Result t;
        match(s, [&](auto state) {
            t.Merge(results_[state]);
            return 0;
        });
        auto& refs = t.refs();
        if (refs.empty()) return false;
        std::sort(refs.begin(), refs.end());
        refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
        matches.clear();
        matches.reserve(refs.size());
        for (auto ref : refs) {
            matches.push_back(inputs_[ref]);
        }
        return true;
    }
};

}  // namespace ac

#include "ac.h"

AhoCorasickTrie::~AhoCorasickTrie() {
    delete static_cast<const ac::Trie<ac::Node2>*>(impl_);
    impl_ = nullptr;
}

bool AhoCorasickTrie::Match(std::string_view s) const {
    return static_cast<const ac::Trie<ac::Node2>*>(impl_)->Match(s);
}

std::unique_ptr<AhoCorasickTrie> AhoCorasickTrie::of(
    std::vector<std::string_view> patterns) {
    std::sort(patterns.begin(), patterns.end());
    patterns.erase(std::unique(patterns.begin(), patterns.end()), patterns.end());
    auto trie = new ac::Trie<ac::Node2>;
    for (auto s : patterns) {
        trie->Add(s);
    }
    trie->Build();
    return std::unique_ptr<AhoCorasickTrie>{new AhoCorasickTrie(trie)};
}

#ifdef AC_TEST
#include <iostream>
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ac <haystack>" << std::endl;
        return 1;
    }
    using namespace std::literals;

    ac::Trie<ac::Node5> trie;
    for (auto s : {"he"sv, "she"sv, "his"sv, "hers"sv}) {
        trie.Add(s);
    }

    trie.Build();
    std::string_view haystack = argv[1];
    std::vector<std::string_view> matches;
    trie.Match(haystack, matches);

    std::cout << "matches:" << std::endl;
    for (auto s : matches) {
        std::cout << s << std::endl;
    }
}
#endif
