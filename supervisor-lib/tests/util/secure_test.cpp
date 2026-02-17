#define BOOST_TEST_MODULE SecureUtilTest
#include <boost/test/unit_test.hpp>
#include "util/secure.h"
#include "util/test_util.h"
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>

namespace fs = std::filesystem;
using TempManager = test_util::TempManager;

using namespace supervisorcpp::util;

// --- sanitize_environment ---

namespace std {
    std::ostream& operator<<(std::ostream& outs, const std::pair<std::string, std::string>& p) {
        return outs << p.first << ": " << p.second;
    }
}

BOOST_AUTO_TEST_CASE(secure__sanitize_environment) {
    using StringsMap = std::map<std::string, std::string>;
    const auto sanitize = [](const StringsMap& env, const StringsMap& exp) {
        const auto res = sanitize_environment(env);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            std::begin(res), std::end(res),
            std::begin(exp), std::end(exp)
        );
    };

    // clean vars pass through
    sanitize({
        {"HOME", "/home/user"},
        {"PATH", "/usr/bin"},
        {"MY_VAR", "value"},
    }, {
        {"HOME", "/home/user"},
        {"PATH", "/usr/bin"},
        {"MY_VAR", "value"},
    });

    // LD_* and similar vars pass through (config file security is the trust boundary)
    sanitize({
        {"LD_PRELOAD", "/opt/lib/preload.so"},
        {"LD_LIBRARY_PATH", "/opt/apps/lib"},
        {"DYLD_INSERT_LIBRARIES", "/opt/lib/insert.dylib"},
        {"SAFE", "ok"},
    }, {
        {"DYLD_INSERT_LIBRARIES", "/opt/lib/insert.dylib"},
        {"LD_LIBRARY_PATH", "/opt/apps/lib"},
        {"LD_PRELOAD", "/opt/lib/preload.so"},
        {"SAFE", "ok"},
    });

    // Invalid key names rejected (non-alphanumeric/underscore)
    sanitize({
        {"GOOD_KEY", "ok"},
        {"BAD-KEY", "rejected"},
        {"BAD KEY", "rejected"},
        {"", "rejected"},
    }, {
        {"GOOD_KEY", "ok"},
    });

    // Null bytes in values rejected
    std::string val_with_null = "hello";
    val_with_null += '\0';
    val_with_null += "world";

    sanitize({
        {"CLEAN", "fine"},
        {"NULLBYTE", val_with_null},
    }, {
        {"CLEAN", "fine"},
    });
}

// --- validate_command_path ---

BOOST_AUTO_TEST_CASE(secure__validate_command_path) {
    // Valid absolute executable
    BOOST_CHECK_NO_THROW(validate_command_path("/bin/sh"));

    // Relative path rejected
    BOOST_CHECK_THROW(validate_command_path("./foo"), SecurityError);
    BOOST_CHECK_THROW(validate_command_path("foo"), SecurityError);

    // Non-existent path rejected
    BOOST_CHECK_THROW(validate_command_path("/nonexistent/binary"), SecurityError);

    // Non-executable file rejected
    const auto noexec = TempManager::config("test content", 0644);
    BOOST_CHECK_THROW(validate_command_path(noexec.str()), SecurityError);
}

// --- validate_canonicalize_path ---

BOOST_AUTO_TEST_CASE(secure__validate_canonicalize_path) {
    auto tmp = fs::canonical(fs::temp_directory_path());

    // Path within allowed prefix resolves fine
    auto result = validate_canonicalize_path(tmp / "somefile.log", tmp);
    BOOST_CHECK(result.string().find(tmp.string()) == 0);

    // Path traversal escaping prefix is rejected
    BOOST_CHECK_THROW(
        validate_canonicalize_path(tmp / ".." / ".." / "etc" / "passwd", tmp),
        SecurityError);
}

// --- validate_log_path ---

BOOST_AUTO_TEST_CASE(secure__validate_log_path) {
    // Use literal /tmp path — fs::temp_directory_path() resolves differently on macOS
    const auto result = validate_log_path("/tmp/test.log");
    BOOST_CHECK(result.string().find("tmp") != std::string::npos);

    // Paths outside allowed prefixes rejected
    BOOST_CHECK_THROW(
        validate_log_path("/home/user/evil.log"),
        SecurityError);
}

// --- validate_pidfile_path ---

BOOST_AUTO_TEST_CASE(secure__validate_pidfile_path) {
    // Valid pidfile in /tmp
    const auto result = validate_pidfile_path("/tmp/test.pid");
    BOOST_CHECK(result.string().find("tmp") != std::string::npos);

    // Valid pidfile in /run (if it exists)
    if (fs::exists("/run")) {
        BOOST_CHECK_NO_THROW(validate_pidfile_path("/run/test.pid"));
    }

    // Relative path rejected
    BOOST_CHECK_THROW(
        validate_pidfile_path("relative.pid"),
        SecurityError);

    // Paths outside allowed prefixes rejected
    BOOST_CHECK_THROW(
        validate_pidfile_path("/home/user/evil.pid"),
        SecurityError);

    // Path traversal rejected
    BOOST_CHECK_THROW(
        validate_pidfile_path("/tmp/../etc/passwd"),
        SecurityError);
}

// --- verify_privilege_drop ---

BOOST_AUTO_TEST_CASE(secure__verify_privilege_drop) {
    const uid_t my_uid = getuid();
    const gid_t my_gid = getgid();

    // Current uid/gid should pass
    BOOST_CHECK_NO_THROW(verify_privilege_drop(my_uid, my_gid));

    // Wrong uid should throw
    BOOST_CHECK_THROW(verify_privilege_drop(my_uid + 1, my_gid), SecurityError);

    // Wrong gid should throw
    BOOST_CHECK_THROW(verify_privilege_drop(my_uid, my_gid + 1), SecurityError);
}

// --- validate_config_file_security ---

BOOST_AUTO_TEST_CASE(secure__validate_config_file_security) {
    // File owned by current user with safe perms — should pass
    const auto good = TempManager::config("good", 0644);
    BOOST_CHECK_NO_THROW(validate_config_file_security(good.path()));

    // World-writable file — should throw
    const auto bad = TempManager::config("good", 0666);
    BOOST_CHECK_THROW(validate_config_file_security(bad.path()), SecurityError);

    // Non-existent file — should throw
    BOOST_CHECK_THROW(
        validate_config_file_security("/tmp/nonexistent_config_test.conf"),
        SecurityError);
}

// --- validate_signal (already partially tested via config, but direct coverage) ---

BOOST_AUTO_TEST_CASE(secure__validate_signal) {
    BOOST_CHECK_NO_THROW(validate_signal("TERM"));
    BOOST_CHECK_NO_THROW(validate_signal("HUP"));
    BOOST_CHECK_NO_THROW(validate_signal("USR1"));
    BOOST_CHECK_THROW(validate_signal("INVALID"), std::invalid_argument);
    BOOST_CHECK_THROW(validate_signal(""), std::invalid_argument);
}
