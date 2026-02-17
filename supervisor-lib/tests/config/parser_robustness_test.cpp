#define BOOST_TEST_MODULE ParserRobustnessTest
#include <boost/test/unit_test.hpp>
#include "../test_util.h"
#include "config/config_parser.h"

using namespace supervisorcpp::config;
using test_util::msg_contains;

BOOST_AUTO_TEST_SUITE(ConfigParserRobustness)

// Test 1: Empty file - should parse successfully with defaults
BOOST_AUTO_TEST_CASE(EmptyFile) {
    const auto config = ConfigParser::parse_string("");
    BOOST_CHECK_EQUAL(config.unix_http_server.socket_file.string(), "/run/supervisord.sock");
}

// Test 2: Missing required sections - should use defaults
BOOST_AUTO_TEST_CASE(MissingRequiredSections) {
    const auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
)");

    BOOST_CHECK_EQUAL(config.programs.size(), 1);
    BOOST_CHECK_EQUAL(config.unix_http_server.socket_file.string(), "/run/supervisord.sock");
}

// Test 3: Invalid INI syntax - unclosed bracket
BOOST_AUTO_TEST_CASE(InvalidSyntaxUnclosedBracket) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[unix_http_server
file=/tmp/test.sock

[supervisord]
logfile=/tmp/test.log
)"),
        ConfigParseError, msg_contains("unmatched '['")
    );
}

// Test 4: Invalid INI syntax - duplicate keys in same section
BOOST_AUTO_TEST_CASE(DuplicateKeysInSection) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[supervisord]
logfile=/tmp/test.log
loglevel=info
loglevel=debug

[program:test]
command=/bin/echo
)"),
        ConfigParseError, msg_contains("duplicate key name")
    );
}

// Test 5: Missing required program fields
BOOST_AUTO_TEST_CASE(MissingProgramCommand) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
directory=/tmp
)"),
        ConfigParseError, msg_contains("missing required 'command'")
    );
}

// Test 6: Invalid numeric values - Boost PropertyTree returns default for invalid integers
BOOST_AUTO_TEST_CASE(InvalidNumericValues) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
startsecs=not_a_number
)"),
        ConfigParseError, msg_contains("startsecs")
    );
}

// Test 7: Negative values where positive expected
BOOST_AUTO_TEST_CASE(NegativeNumericValues) {
    const auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
startsecs=-5
)");

    BOOST_CHECK_EQUAL(config.programs[0].startsecs, -5);
}

// Test 8: Invalid boolean values
BOOST_AUTO_TEST_CASE(InvalidBooleanValues) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
autorestart=maybe
)"),
        ConfigParseError, msg_contains("autorestart")
    );
}

// Test 9: Very long values
BOOST_AUTO_TEST_CASE(VeryLongValues) {
    const std::string long_command(10000, 'x');

    // Command must be absolute path - this should fail validation
    BOOST_CHECK_EXCEPTION(ConfigParser::parse_string(R"(
[program:test]
command=)" + long_command), ConfigParseError, msg_contains("absolute path"));
}

// Test 10: Special characters in values
BOOST_AUTO_TEST_CASE(SpecialCharactersInValues) {
    const auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo "hello \"world\" $PATH & < > |"
)");

    BOOST_CHECK_EQUAL(config.programs[0].command, R"(/bin/echo "hello \"world\" $PATH & < > |")");
}

// Test 11: Non-existent file (must use parse_file)
BOOST_AUTO_TEST_CASE(NonExistentFile) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_file("/nonexistent/path/to/file.ini"),
        ConfigParseError, msg_contains("not found")
    );
}

// Test 12: Invalid log level
BOOST_AUTO_TEST_CASE(InvalidLogLevel) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[supervisord]
logfile=/tmp/test.log
loglevel=invalid_level

[program:test]
command=/bin/echo
)"),
        ConfigParseError, msg_contains("loglevel")
    );
}

// Test 13: Invalid file size format
BOOST_AUTO_TEST_CASE(InvalidFileSizeFormat) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
stdout_logfile_maxbytes=50XYZ
)"),
        ConfigParseError, msg_contains("Invalid size format")
    );
}

// Test 14: Valid file size suffixes
BOOST_AUTO_TEST_CASE(ValidFileSizeSuffixes) {
    const auto config1 = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
stdout_logfile_maxbytes=1KB
)");
    BOOST_CHECK_EQUAL(config1.programs[0].stdout_log.file_maxbytes, 1024);

    const auto config2 = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
stdout_logfile_maxbytes=2MB
)");
    BOOST_CHECK_EQUAL(config2.programs[0].stdout_log.file_maxbytes, 2 * 1024 * 1024);

    const auto config3 = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
stdout_logfile_maxbytes=1GB
)");
    BOOST_CHECK_EQUAL(config3.programs[0].stdout_log.file_maxbytes, 1024ULL * 1024 * 1024);
}

// Test 15: Invalid signal names
BOOST_AUTO_TEST_CASE(InvalidSignalName) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
stopsignal=INVALID_SIGNAL
)"),
        ConfigParseError, msg_contains("Invalid signal")
    );
}

// Test 16: Valid signal names
BOOST_AUTO_TEST_CASE(ValidSignalNames) {
    const std::string valid_signals[] = {
        "TERM", "HUP", "INT", "QUIT", "KILL", "USR1", "USR2"
    };

    for (const auto& sig : valid_signals) {
        const auto content = R"(
[program:test]
command=/bin/echo
stopsignal=)" + sig;

        const auto config = ConfigParser::parse_string(content);
        BOOST_CHECK_EQUAL(config.programs[0].stopsignal, sig);
    }
}

// Test 17: Multiple programs with same name
BOOST_AUTO_TEST_CASE(DuplicateProgramNames) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo first

[program:test]
command=/bin/echo second
)"),
        ConfigParseError, msg_contains("duplicate section name")
    );
}

// Test 18: Environment variable syntax validation
BOOST_AUTO_TEST_CASE(EnvironmentVariableSyntax) {
    auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
environment=KEY1="value1",KEY2="value2",KEY3="value with spaces"
)");

    BOOST_CHECK_EQUAL(config.programs[0].environment.size(), 3);
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY1"], "value1");
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY2"], "value2");
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY3"], "value with spaces");
}

// Test 19: Malformed environment variables
BOOST_AUTO_TEST_CASE(MalformedEnvironmentVariables) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
environment=INVALID_NO_EQUALS
)"),
        ConfigParseError, msg_contains("environment")
    );
}

// Test 19b: Empty key in environment rejected by grammar
BOOST_AUTO_TEST_CASE(EnvironmentEmptyKey) {
    BOOST_CHECK_EXCEPTION(
        ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
environment==value
)"),
        ConfigParseError, msg_contains("environment")
    );
}

// Test 20: Variable substitution edge cases
BOOST_AUTO_TEST_CASE(VariableSubstitutionEdgeCases) {
    const auto config = ConfigParser::parse_string(R"(
[program:test_prog]
command=/bin/echo %(program_name)s
stdout_logfile=/var/log/%(program_name)s.log
)");

    BOOST_CHECK_EQUAL(config.programs[0].command, "/bin/echo test_prog");
    BOOST_CHECK_EQUAL(config.programs[0].stdout_log.file->string(), std::filesystem::weakly_canonical("/var/log/test_prog.log"));
}

// Test 21: Unknown variable in substitution
BOOST_AUTO_TEST_CASE(UnknownVariableSubstitution) {
    const auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo %(unknown_var)s
)");

    // Unknown variables should remain unchanged
    BOOST_CHECK_EQUAL(config.programs[0].command, "/bin/echo %(unknown_var)s");
}

// Test 22: Whitespace handling
BOOST_AUTO_TEST_CASE(WhitespaceHandling) {
    const auto config = ConfigParser::parse_string(R"(
[unix_http_server]
file  =  /tmp/test.sock

[supervisord]
logfile = /tmp/test.log

[supervisorctl]
serverurl = unix:///tmp/test.sock

[program:test]
command = /bin/echo   hello   world
)");

    // Boost PropertyTree trims leading/trailing whitespace around '='
    BOOST_CHECK_EQUAL(config.programs[0].command, "/bin/echo   hello   world");
}

// Test 23: Comments in config
BOOST_AUTO_TEST_CASE(CommentsInConfig) {
    const auto config = ConfigParser::parse_string(R"(
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

    BOOST_CHECK_EQUAL(config.programs.size(), 1);
}

// Test 24: Commas inside quoted environment values
BOOST_AUTO_TEST_CASE(EnvironmentQuotedCommas) {
    auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
environment=OPTS="--flag=a,b,c",SIMPLE=1
)");

    BOOST_CHECK_EQUAL(config.programs[0].environment.size(), 2);
    BOOST_CHECK_EQUAL(config.programs[0].environment["OPTS"], "--flag=a,b,c");
    BOOST_CHECK_EQUAL(config.programs[0].environment["SIMPLE"], "1");
}

// Test 25: Escaped quotes inside environment values
BOOST_AUTO_TEST_CASE(EnvironmentEscapedQuotes) {
    auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
environment=MSG="say \"hello\"",PATH="/usr/bin"
)");

    BOOST_CHECK_EQUAL(config.programs[0].environment.size(), 2);
    BOOST_CHECK_EQUAL(config.programs[0].environment["MSG"], "say \"hello\"");
    BOOST_CHECK_EQUAL(config.programs[0].environment["PATH"], "/usr/bin");
}

// Test 26: Single-quoted environment values
BOOST_AUTO_TEST_CASE(EnvironmentSingleQuoted) {
    auto config = ConfigParser::parse_string(R"(
[program:test]
command=/bin/echo
environment=KEY1='value,with,commas',KEY2="double quoted"
)");

    BOOST_CHECK_EQUAL(config.programs[0].environment.size(), 2);
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY1"], "value,with,commas");
    BOOST_CHECK_EQUAL(config.programs[0].environment["KEY2"], "double quoted");
}

BOOST_AUTO_TEST_SUITE_END()
