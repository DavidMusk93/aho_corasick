#include "ac.h"
#include <iostream>

int main() {
    using namespace std::literals;
    auto trie = AhoCorasickTrie::of({"he"sv, "she"sv, "his"sv, "hers"sv});
    std::cout << trie->Match("ushers") << std::endl;
}
