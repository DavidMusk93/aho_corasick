#pragma once

#include <memory>
#include <string>
#include <vector>

class AhoCorasickTrie {
    const void* impl_;

    AhoCorasickTrie(const void* impl) : impl_(impl) {}

   public:
    ~AhoCorasickTrie();
    bool Match(std::string_view s) const;
    static std::unique_ptr<AhoCorasickTrie> of(std::vector<std::string_view> patterns);
};
