#pragma once
#ifndef SUPERVISOR_APP__ARGS_PARSER
#define SUPERVISOR_APP__ARGS_PARSER

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <boost/program_options.hpp>

namespace supervisorcpp {
    namespace po = boost::program_options;

    using FuncParser = std::function<void (po::command_line_parser&, po::options_description&)>;
    using FuncHelp = std::function<void ()>;
    using ParsedArgs = std::pair<
        po::variables_map,
        std::vector<std::string>
    >;
    using ParsedArgsOpt = std::optional<ParsedArgs>;
    ParsedArgsOpt parse_args(int argc, char* argv[], const std::string& caption, FuncParser func_parser, FuncHelp func_help = FuncHelp{});

} // namespace supervisorcpp

#endif // SUPERVISOR_APP__ARGS_PARSER
