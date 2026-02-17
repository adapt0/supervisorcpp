// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#define BOOST_TEST_MODULE LogWriterTest
#include <boost/test/unit_test.hpp>
#include "logger/log_writer.h"
#include "util/test_util.h"
#include <cassert>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using TempManager = test_util::TempManager;

using namespace supervisorcpp::logger;

BOOST_AUTO_TEST_CASE(log_writer__write_and_buffering) {
    const auto dir = TempManager::dir("log_writer_buf");
    const auto logfile = dir.path() / "test.log";

    {
        LogWriter writer(logfile, 0 /* no rotation */, 0);

        // Complete line is written to disk
        writer.write("hello world\n");
        BOOST_CHECK_EQUAL(test_util::read_file(logfile), "hello world\n");
        BOOST_CHECK_EQUAL(writer.current_size(), 12);

        // Partial write (no newline) stays buffered — file unchanged
        writer.write("partial");
        BOOST_CHECK_EQUAL(test_util::read_file(logfile), "hello world\n");
        BOOST_CHECK_EQUAL(writer.current_size(), 12);

        // Completing the line flushes everything
        writer.write(" data\n");
        BOOST_CHECK_EQUAL(test_util::read_file(logfile), "hello world\npartial data\n");
        BOOST_CHECK_EQUAL(writer.current_size(), 25);

        // write_line appends \n automatically
        writer.write_line("auto newline");
        BOOST_CHECK(test_util::read_file(logfile).find("auto newline\n") != std::string::npos);

        // flush() forces partial buffer to disk
        writer.write("no newline yet");
        BOOST_CHECK(test_util::read_file(logfile).find("no newline yet") == std::string::npos);
        writer.flush();
        BOOST_CHECK(test_util::read_file(logfile).find("no newline yet") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(log_writer__rotation) {
    const auto dir = TempManager::dir("log_writer_rot");
    const auto logfile = dir.path() / "test.log";

    {
        // Small max_bytes to trigger rotation quickly
        LogWriter writer(logfile, 50, 2);

        // Write enough to trigger rotation (>50 bytes)
        for (int i = 0; i < 5; i++) {
            writer.write_line("line " + std::to_string(i) + " padding data here");
        }

        // Backup .1 should exist after rotation
        BOOST_CHECK(fs::exists(logfile));
        BOOST_CHECK(fs::exists(fs::path(logfile.string() + ".1")));

        // Main file should be smaller than max_bytes (freshly rotated)
        BOOST_CHECK_LE(writer.current_size(), 50);
    }

    // Test backup pruning with backups=2
    {
        assert(!dir.path().empty());
        fs::remove_all(dir.path());
        fs::create_directories(dir.path());

        LogWriter writer{logfile, 30, 2};

        // Trigger multiple rotations
        for (int i = 0; i < 20; i++) {
            writer.write_line("rotation test line " + std::to_string(i));
        }

        // .1 and .2 should exist, .3 should not (pruned)
        BOOST_CHECK(fs::exists(fs::path(logfile.string() + ".1")));
        BOOST_CHECK(fs::exists(fs::path(logfile.string() + ".2")));
        BOOST_CHECK(!fs::exists(fs::path(logfile.string() + ".3")));
    }
}

BOOST_AUTO_TEST_CASE(log_writer__directory_creation) {
    const auto dir = TempManager::dir("log_writer_nested");
    const auto logfile = dir.path() / "sub" / "dir" / "test.log";

    {
        LogWriter writer(logfile, 0, 0);
        writer.write_line("created");

        BOOST_CHECK(fs::exists(logfile));
        BOOST_CHECK_EQUAL(test_util::read_file(logfile), "created\n");
    }
}
