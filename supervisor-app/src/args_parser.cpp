// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "args_parser.h"
#include "version.h"
#include "logger/logger.h"
#include <iostream>

namespace supervisorcpp {

// https://stackoverflow.com/questions/59761124/use-boostprogram-options-to-specify-multiple-flags
class CountValue : public po::typed_value<std::size_t> {
public:    
    CountValue()
    : po::typed_value<std::size_t>{&store_}
    , store_{0}
    { 
        default_value(0);
        zero_tokens();
    }

    ~CountValue() override { }

    void xparse(boost::any& store, const std::vector<std::string>& /*tokens*/) const override {
        // Replace the stored value with the access count
        store_ = ++count_;
        store = boost::any(store_);
    }

private:
    mutable std::size_t count_{ 0 };
    mutable size_t store_;
};


ParsedArgsOpt parse_args(int argc, char* argv[], const std::string& caption, FuncParser func_parser, FuncHelp func_help) {
    po::command_line_parser parser{argc, argv};

    po::options_description desc{caption};
    desc.add_options()
        ("help,h", "Show this help message")
        ("version", "Show version information")
        ("configuration,c", po::value<std::string>()->default_value("/etc/supervisord.conf"), "Configuration file path")
        ("verbose,v", new CountValue{}, "Increase logging verbosity (-v, -vv, -vvv)")
    ;
    if (func_parser) func_parser(parser, desc);

    const auto parsed = parser
        .options(desc)
        .run()
    ;

    po::variables_map vm;
    po::store(parsed, vm);
    po::notify(vm);

    auto args = po::collect_unrecognized(parsed.options, po::include_positional);

    if (vm.count("version")) {
        std::cout << VERSION_STR << std::endl;
        return ParsedArgsOpt{};
    }

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        if (func_help) func_help();
        return ParsedArgsOpt{};
    }

    if (const auto verbosity = vm["verbose"].as<size_t>(); verbosity > 0) {
        logger::increment_log_level(verbosity);
    }

    return ParsedArgs{std::move(vm), std::move(args)};
}

} // namespace supervisorcpp
