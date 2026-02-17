#pragma once
#ifndef SUPERVISOR_LIB__CONFIG__INI_READER
#define SUPERVISOR_LIB__CONFIG__INI_READER

#include <istream>
#include <streambuf>
#include <string>

namespace supervisorcpp::config {

/**
 * Filtering streambuf that strips inline ; comments before INI parsing.
 * Supervisord-style: ';' preceded by whitespace is treated as a comment.
 */
class CommentStrippingBuf : public std::streambuf {
public:
    explicit CommentStrippingBuf(std::istream& source) : source_(source) {}

protected:
    int_type underflow() override {
        if (!std::getline(source_, buf_)) return traits_type::eof();
        buf_ = strip_inline_comment_(buf_);
        buf_ += '\n';
        setg(buf_.data(), buf_.data(), buf_.data() + buf_.size());
        return traits_type::to_int_type(*gptr());
    }

private:
    static std::string strip_inline_comment_(const std::string& line) {
        for (size_t i = 1; i < line.size(); ++i) {
            if (line[i] == ';' && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
                const auto end = line.find_last_not_of(" \t", i - 1);
                return (end != std::string::npos) ? line.substr(0, end + 1) : "";
            }
        }
        return line;
    }

    std::istream& source_;
    std::string buf_;
};

} // namespace supervisorcpp::config

#endif // SUPERVISOR_LIB__CONFIG__INI_READER
