#define BOOST_TEST_MODULE ParserRobustnessTest
#include <boost/test/unit_test.hpp>
#include "config_parser.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace supervisord::config;

// Helper to create temporary test files
class TempConfigFile {
public:
    TempConfigFile(const std::string& content) {
        path_ = fs::temp_directory_path() / ("test_config_" + std::to_string(counter_++) + ".ini");
        std::ofstream file(path_);
        file << content;
        file.close();
    }

    ~TempConfigFile() {
        if (fs::exists(path_)) {
            fs::remove(path_);
        }
    }

    fs::path path() const { return path_; }

private:
    fs::path path_;
    static int counter_;
};

int TempConfigFile::counter_ = 0;

BOOST_AUTO_TEST_SUITE(ConfigParserRobustness)

// Test 1: Empty file - should parse successfully with defaults
BOOST_AUTO_TEST_CASE(EmptyFile) {
    TempConfigFile temp("");

    // Empty file parses successfully but uses default values
    auto config = ConfigParser::parse_file(temp.path());
    // Default socket file is set
    BOOST_CHECK(!config.unix_http_server.socket_file.empty());
}

// Test 2: Missing required sections - should use defaults
BOOST_AUTO_TEST_CASE(MissingRequiredSections) {
    TempConfigFile temp(R"(
[program:test]
command=/bin/echo
)");

    // Parse succeeds with default values for missing sections
    auto config = ConfigParser::parse_file(temp.path());
    BOOST_CHECK_EQUAL(config.programs.size(), 1);
    BOOST_CHECK(!config.unix_http_server.socket_file.empty()); // Has default
}

// Test 3: Invalid INI syntax - unclosed bracket
BOOST_AUTO_TEST_CASE(InvalidSyntaxUnclosedBracket) {
    TempConfigFile temp(R"(
[unix_http_server
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log
)");

    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        std::exception  // Boost.PropertyTree throws generic exception
    );
}

// Test 4: Invalid INI syntax - duplicate keys in same section
BOOST_AUTO_TEST_CASE(DuplicateKeysInSection) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log
loglevel=info
loglevel=debug

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
)");

    // Boost PropertyTree correctly rejects duplicate keys
    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        ConfigParseError
    );
}

// Test 5: Missing required program fields
BOOST_AUTO_TEST_CASE(MissingProgramCommand) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
directory=/tmp
)");

    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        ConfigParseError
    );
}

// Test 6: Invalid numeric values - Boost PropertyTree returns 0 for invalid integers
BOOST_AUTO_TEST_CASE(InvalidNumericValues) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
startsecs=not_a_number
)");

    // Boost PropertyTree silently uses the default value for invalid integers
    auto config = ConfigParser::parse_file(temp.path());
    // Default value from struct initializer is 1
    BOOST_CHECK_EQUAL(config.programs[0].startsecs, 1);
}

// Test 7: Negative values where positive expected
BOOST_AUTO_TEST_CASE(NegativeNumericValues) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
startsecs=-5
)");

    // Should parse but the value will be negative (we could add validation)
    auto config = ConfigParser::parse_file(temp.path());
    BOOST_CHECK_EQUAL(config.programs[0].startsecs, -5);
}

// Test 8: Invalid boolean values
BOOST_AUTO_TEST_CASE(InvalidBooleanValues) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
autorestart=maybe
)");

    // Boost PropertyTree will likely treat this as false or throw
    auto config = ConfigParser::parse_file(temp.path());
    // The parser should handle this gracefully
}

// Test 9: Very long values
BOOST_AUTO_TEST_CASE(VeryLongValues) {
    std::string long_command(10000, 'x');
    std::string content = R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=)" + long_command;

    TempConfigFile temp(content);
    // Command must be absolute path - this should fail validation
    BOOST_CHECK_THROW(ConfigParser::parse_file(temp.path()), ConfigParseError);
}

// Test 10: Special characters in values
BOOST_AUTO_TEST_CASE(SpecialCharactersInValues) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo "hello \"world\" $PATH & < > |"
)");

    auto config = ConfigParser::parse_file(temp.path());
    BOOST_CHECK(config.programs[0].command.find("\"") != std::string::npos);
}

// Test 11: Non-existent file
BOOST_AUTO_TEST_CASE(NonExistentFile) {
    BOOST_CHECK_THROW(
        ConfigParser::parse_file("/nonexistent/path/to/file.ini"),
        ConfigParseError
    );
}

// Test 12: Invalid log level
BOOST_AUTO_TEST_CASE(InvalidLogLevel) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log
loglevel=invalid_level

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
)");

    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        ConfigParseError
    );
}

// Test 13: Invalid file size format
BOOST_AUTO_TEST_CASE(InvalidFileSizeFormat) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
stdout_logfile_maxbytes=50XYZ
)");

    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        std::exception
    );
}

// Test 14: Valid file size suffixes
BOOST_AUTO_TEST_CASE(ValidFileSizeSuffixes) {
    // Test KB
    TempConfigFile temp1(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
stdout_logfile_maxbytes=1KB
)");

    auto config1 = ConfigParser::parse_file(temp1.path());
    BOOST_CHECK_EQUAL(config1.programs[0].stdout_logfile_maxbytes, 1024);

    // Test MB
    TempConfigFile temp2(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
stdout_logfile_maxbytes=2MB
)");

    auto config2 = ConfigParser::parse_file(temp2.path());
    BOOST_CHECK_EQUAL(config2.programs[0].stdout_logfile_maxbytes, 2 * 1024 * 1024);

    // Test GB
    TempConfigFile temp3(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
stdout_logfile_maxbytes=1GB
)");

    auto config3 = ConfigParser::parse_file(temp3.path());
    BOOST_CHECK_EQUAL(config3.programs[0].stdout_logfile_maxbytes, 1024ULL * 1024 * 1024);
}

// Test 15: Invalid signal names
BOOST_AUTO_TEST_CASE(InvalidSignalName) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
stopsignal=INVALID_SIGNAL
)");

    // Should throw or default to TERM
    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        ConfigParseError
    );
}

// Test 16: Valid signal names
BOOST_AUTO_TEST_CASE(ValidSignalNames) {
    std::vector<std::string> valid_signals = {"TERM", "HUP", "INT", "QUIT", "KILL", "USR1", "USR2"};

    for (const auto& sig : valid_signals) {
        std::string content = R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
stopsignal=)" + sig;

        TempConfigFile temp(content);
        auto config = ConfigParser::parse_file(temp.path());
        BOOST_CHECK_EQUAL(config.programs[0].stopsignal, sig);
    }
}

// Test 17: Multiple programs with same name
BOOST_AUTO_TEST_CASE(DuplicateProgramNames) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo first

[program:test]
command=/bin/echo second
)");

    // Boost PropertyTree correctly rejects duplicate section names
    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        ConfigParseError
    );
}

// Test 18: Environment variable syntax validation
BOOST_AUTO_TEST_CASE(EnvironmentVariableSyntax) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
environment=KEY1="value1",KEY2="value2",KEY3="value with spaces"
)");

    auto config = ConfigParser::parse_file(temp.path());
    BOOST_CHECK_EQUAL(config.programs[0].environment.size(), 3);
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY1"], "value1");
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY2"], "value2");
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY3"], "value with spaces");
}

// Test 19: Malformed environment variables
BOOST_AUTO_TEST_CASE(MalformedEnvironmentVariables) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo
environment=INVALID_NO_EQUALS
)");

    // Should handle gracefully or throw
    BOOST_CHECK_THROW(
        ConfigParser::parse_file(temp.path()),
        ConfigParseError
    );
}

// Test 20: Variable substitution edge cases
BOOST_AUTO_TEST_CASE(VariableSubstitutionEdgeCases) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test_prog]
command=/bin/echo %(program_name)s
stdout_logfile=/var/log/%(program_name)s.log
)");

    auto config = ConfigParser::parse_file(temp.path());
    BOOST_CHECK_EQUAL(config.programs[0].command, "/bin/echo test_prog");
    BOOST_CHECK_EQUAL(config.programs[0].stdout_logfile->string(), "/var/log/test_prog.log");
}

// Test 21: Unknown variable in substitution
BOOST_AUTO_TEST_CASE(UnknownVariableSubstitution) {
    TempConfigFile temp(R"(
[unix_http_server]
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo %(unknown_var)s
)");

    auto config = ConfigParser::parse_file(temp.path());
    // Unknown variables should remain unchanged
    BOOST_CHECK(config.programs[0].command.find("%(unknown_var)s") != std::string::npos);
}

// Test 22: Whitespace handling
BOOST_AUTO_TEST_CASE(WhitespaceHandling) {
    TempConfigFile temp(R"(
[unix_http_server]
file  =  /tmp/test.sock

[supervisord]
logfile = /tmp/test.log

[supervisorctl]
serverurl = unix:///tmp/test.sock

[program:test]
command = /bin/echo   hello   world
)");

    auto config = ConfigParser::parse_file(temp.path());
    // Boost PropertyTree trims values by default
    BOOST_CHECK(config.programs[0].command.find("echo") != std::string::npos);
}

// Test 23: Comments in config
BOOST_AUTO_TEST_CASE(CommentsInConfig) {
    TempConfigFile temp(R"(
# This is a comment
[unix_http_server]
file=/tmp/test.sock  ; inline comment

[supervisord]
logfile=/tmp/test.log
; Another comment

[supervisorctl]
serverurl=unix:///tmp/test.sock

[program:test]
command=/bin/echo  # This should work
)");

    auto config = ConfigParser::parse_file(temp.path());
    BOOST_CHECK_EQUAL(config.programs.size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
