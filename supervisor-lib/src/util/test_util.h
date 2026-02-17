// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#ifndef SUPERVISORCPP_TEST_UTIL_H
#define SUPERVISORCPP_TEST_UTIL_H

#ifndef BOOST_TEST_MODULE
#   error Expected to be part of a unit test
#endif // BOOST_TEST_MODULE

#include "logger/logger.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <boost/test/unit_test.hpp>

/// adjust test logging level, debug to ignore unless a debug arg is specified
/// use ./TEST -- debug (./ctl_handler_test -- --debug)
struct TestLoggingFixture {
    TestLoggingFixture() {
        using supervisorcpp::logger::LogLevel;
        auto level = LogLevel::IGNORE;

        const auto& suite = boost::unit_test::framework::master_test_suite();
        for (int i = 1; i < suite.argc; ++i) {
            if (suite.argv[i] == std::string_view{"--debug"}
                || suite.argv[i] == std::string_view{"debug"}
            ) {
                level = LogLevel::TRACE;
                break;
            }
        }
        init_logging(level);
    }
};
BOOST_GLOBAL_FIXTURE(TestLoggingFixture);

namespace test_util {

namespace fs = std::filesystem;

// read entire file contents
inline std::string read_file(const fs::path& path) {
    std::ifstream ifs{path, std::ios::binary};
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

inline auto true_exe() {
    static const std::string BIN_TRUE = std::filesystem::exists("/usr/bin/true") ? "/usr/bin/true" : "/bin/true";
    return BIN_TRUE;
}
inline auto false_exe() {
    static const std::string BIN_FALSE = std::filesystem::exists("/usr/bin/false") ? "/usr/bin/false" : "/bin/false";
    return BIN_FALSE;
}


/// temporary file manager singleton
class TempManager {
    struct UseManager { };
public:
    static auto& instance() {
        static TempManager temp_manager;
        return temp_manager;
    }

    ~TempManager() {
        check_remove_all_(path_);
    }

    TempManager(const TempManager&) = delete;
    TempManager& operator=(const TempManager&) = delete;
    TempManager(TempManager&&) = delete;
    TempManager& operator=(TempManager&&) = delete;

    struct Cleanup {
        using Func = std::function<void (const fs::path&)>;

        Cleanup(UseManager, fs::path p, Func func)
        : path_{std::move(p)}
        , cleanup_(std::move(func))
        { }
        ~Cleanup() {
            if (cleanup_ && !path_.empty()) cleanup_(path_);
        }

        Cleanup(Cleanup&&) = default;
        Cleanup& operator=(Cleanup&&) = default;

        Cleanup(const Cleanup&) = delete;
        Cleanup& operator=(const Cleanup&) = delete;

        const auto& path() const noexcept { return path_; }
        const char* c_str() const noexcept { return path_.c_str(); }
        auto str() const { return path_.string(); }

    private:
        fs::path    path_;
        Func        cleanup_;
    };

    static Cleanup config(const std::string& content, mode_t mode = 0644) {
        auto tmp = file("test_" + std::to_string(instance().counter_++) + ".cfg");
        std::ofstream{tmp.path()} << content;
        chmod(tmp.c_str(), mode);
        return tmp;
    }

    static Cleanup dir(const std::string& name) {
        const auto& tmp_path = instance().path_;
        if (tmp_path.empty()) throw std::runtime_error("Missing temporary path");

        auto path = tmp_path / name;
        fs::create_directories(path);

        return Cleanup{
            UseManager{},
            std::move(path),
            check_remove_all_
        };
    }

    static Cleanup file(const std::string& name) {
        const auto& tmp_path = instance().path_;
        if (tmp_path.empty()) throw std::runtime_error("Missing temporary path");

        return Cleanup{
            UseManager{},
            tmp_path / name,
            [](const fs::path& path){
                std::error_code ec;
                fs::remove(path, ec);
            }
        };
    }

private:
    TempManager() {
        path_ = [
            tmp_base{fs::temp_directory_path()}
        ]() {
            std::random_device rd;
            for (int i = 0; i < 1000; ++i) {
                auto tmp = tmp_base / ("__test__" + std::to_string(rd()));
                std::error_code ec;
                if (!fs::exists(tmp, ec) && !ec) return tmp;
            }
            throw std::runtime_error("Failed to determine a temporary path");
        }();

        fs::create_directory(path_);
    }

    static void check_remove_all_(const fs::path& path) {
        if (path.empty()) return;

        const auto path_str = path.string();
        auto is_under = [&](const fs::path& base) {
            const auto base_str = base.string();
            return path_str.size() > base_str.size() && path_str.starts_with(base_str);
        };

        if (is_under(fs::temp_directory_path())) {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    }

    fs::path    path_;
    int         counter_{0};
};

/// Predicate for BOOST_CHECK_EXCEPTION that verifies e.what() contains substr.
/// On mismatch, re-throws so Boost.Test reports the actual error message.
inline auto msg_contains(std::string_view substr) {
    return [=](const std::exception& e) {
        if (!std::string_view{e.what()}.contains(substr)) throw;
        return true;
    };
}

} // namespace test_util

#endif // SUPERVISORCPP_TEST_UTIL_H
