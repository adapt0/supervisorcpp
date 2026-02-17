#include "string.h"
#include <unordered_map>

namespace supervisorcpp::util {

std::string escape_str(const std::string_view& str, const Needles& needles) {
    std::string out;
    out.reserve(str.size());

    size_t last = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        const auto fnd = needles.find(str[i]);
        if (std::end(needles) != fnd) {
            out.append(str, last, i - last);
            out.append(fnd->second);
            last = i + 1;
        }
    }
    out.append(str, last);

    return out;
}

std::string escape_xml(const std::string_view& str) {
    static const util::Needles needles{
        { '"',  "&quot;" },
        { '\'', "&apos;" },
        { '<',  "&lt;" },
        { '>',  "&gt;" },
        { '&',  "&amp;"},
    };
    return escape_str(str, needles);
}

std::string glob_to_regex(const std::string_view& str) {
    static const util::Needles needles{
        { '.',  "\\." },
        { '^',  "\\^" },
        { '$',  "\\$" },
        { '+',  "\\+" },
        { '(',  "\\(" },
        { ')',  "\\)" },
        { '[',  "\\[" },
        { ']',  "\\]" },
        { '{',  "\\{" },
        { '}',  "\\}" },
        { '\\', "\\\\" },
        { '|',  "\\|" },
        { '*',  ".*"  }, // glob all
        { '?',  "."   }, // glob single
    };
    return escape_str(str, needles);
}

} // namespace supervisorcpp::util
