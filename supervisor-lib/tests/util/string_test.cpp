// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#define BOOST_TEST_MODULE XmlRpcTest
#include <boost/test/unit_test.hpp>
#include "util/string.h"
#include <regex>

namespace util = supervisorcpp::util;

BOOST_AUTO_TEST_CASE(string__escape_xml) {
    BOOST_CHECK_EQUAL(util::escape_xml(""), "");
    BOOST_CHECK_EQUAL(util::escape_xml("test"), "test");
    BOOST_CHECK_EQUAL(util::escape_xml("&"), "&amp;");
    BOOST_CHECK_EQUAL(util::escape_xml("\"'<>&"), "&quot;&apos;&lt;&gt;&amp;");
}

BOOST_AUTO_TEST_CASE(string__glob_to_regex) {
    // Empty and plain strings
    BOOST_CHECK_EQUAL(util::glob_to_regex(""), "");
    BOOST_CHECK_EQUAL(util::glob_to_regex("test"), "test");

    // Glob wildcards
    BOOST_CHECK_EQUAL(util::glob_to_regex("*"), ".*");
    BOOST_CHECK_EQUAL(util::glob_to_regex("?"), ".");

    // Regex metacharacters that need escaping
    BOOST_CHECK_EQUAL(util::glob_to_regex("."), "\\.");
    BOOST_CHECK_EQUAL(util::glob_to_regex("^$+()[]{}|\\"), 
                      "\\^\\$\\+\\(\\)\\[\\]\\{\\}\\|\\\\");

    // Realistic glob patterns
    BOOST_CHECK_EQUAL(util::glob_to_regex("*.txt"), ".*\\.txt");
    BOOST_CHECK_EQUAL(util::glob_to_regex("file?.log"), "file.\\.log");
    BOOST_CHECK_EQUAL(util::glob_to_regex("data[1].csv"), "data\\[1\\]\\.csv");
    BOOST_CHECK_EQUAL(util::glob_to_regex("report (final).pdf"), 
                      "report \\(final\\)\\.pdf");
}

BOOST_AUTO_TEST_CASE(string__glob_to_regex__functional) {
    std::regex re(util::glob_to_regex("*.txt"));
    BOOST_CHECK(std::regex_match("file.txt", re));
    BOOST_CHECK(std::regex_match(".txt", re));
    BOOST_CHECK(!std::regex_match("file.txt.bak", re));
    BOOST_CHECK(!std::regex_match("file.csv", re));

    std::regex re2(util::glob_to_regex("data?.csv"));
    BOOST_CHECK(std::regex_match("data1.csv", re2));
    BOOST_CHECK(std::regex_match("dataX.csv", re2));
    BOOST_CHECK(!std::regex_match("data12.csv", re2));
    BOOST_CHECK(!std::regex_match("data.csv", re2));
}
