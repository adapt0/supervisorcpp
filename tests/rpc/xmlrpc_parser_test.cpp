#define BOOST_TEST_MODULE XmlRpcParserTest
#include <boost/test/unit_test.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace pt = boost::property_tree;

/**
 * Helper struct for process info (matching supervisorctl.cpp)
 */
struct ProcessInfo {
    std::string name;
    std::string statename;
    int pid = -1;
    std::string description;
};

/**
 * Parse process info array from XML-RPC response
 * (Simplified version of supervisorctl.cpp logic for testing)
 */
std::vector<ProcessInfo> parse_process_info_array(const std::string& xml) {
    std::vector<ProcessInfo> result;

    try {
        std::istringstream iss(xml);
        pt::ptree tree;
        pt::read_xml(iss, tree);

        // Navigate to the array
        auto value_node = tree.get_child("methodResponse.params.param.value");

        if (auto array_node = value_node.get_child_optional("array.data")) {
            for (const auto& item : *array_node) {
                if (item.first == "value") {
                    ProcessInfo info;

                    // Parse struct members
                    if (auto struct_node = item.second.get_child_optional("struct")) {
                        for (const auto& member : *struct_node) {
                            if (member.first == "member") {
                                std::string name = member.second.get<std::string>("name", "");

                                if (name == "name") {
                                    info.name = member.second.get<std::string>("value.string", "");
                                } else if (name == "statename") {
                                    info.statename = member.second.get<std::string>("value.string", "UNKNOWN");
                                } else if (name == "pid") {
                                    info.pid = member.second.get<int>("value.int", -1);
                                } else if (name == "description") {
                                    info.description = member.second.get<std::string>("value.string", "");
                                }
                            }
                        }
                    }

                    result.push_back(info);
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse response: " + std::string(e.what()));
    }

    return result;
}

/**
 * Parse a simple boolean response
 */
bool parse_boolean_response(const std::string& xml) {
    try {
        std::istringstream iss(xml);
        pt::ptree tree;
        pt::read_xml(iss, tree);

        return tree.get<bool>("methodResponse.params.param.value.boolean");
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse boolean response: " + std::string(e.what()));
    }
}

/**
 * Parse a simple string response
 */
std::string parse_string_response(const std::string& xml) {
    try {
        std::istringstream iss(xml);
        pt::ptree tree;
        pt::read_xml(iss, tree);

        return tree.get<std::string>("methodResponse.params.param.value.string");
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse string response: " + std::string(e.what()));
    }
}

BOOST_AUTO_TEST_SUITE(XmlRpcParserRobustness)

// Test 1: Valid process info array response
BOOST_AUTO_TEST_CASE(ValidProcessInfoArray) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test_proc</string></value></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
                <member><name>pid</name><value><int>1234</int></value></member>
                <member><name>description</name><value><string>pid 1234, uptime 0:00:10</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "test_proc");
    BOOST_CHECK_EQUAL(processes[0].statename, "RUNNING");
    BOOST_CHECK_EQUAL(processes[0].pid, 1234);
    BOOST_CHECK_EQUAL(processes[0].description, "pid 1234, uptime 0:00:10");
}

// Test 2: Empty array response
BOOST_AUTO_TEST_CASE(EmptyArrayResponse) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 0);
}

// Test 3: Malformed XML - unclosed tag
BOOST_AUTO_TEST_CASE(MalformedXmlUnclosedTag) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <string>test
      </value>
    </param>
  </params>
</methodResponse>)";

    BOOST_CHECK_THROW(
        parse_string_response(xml),
        std::exception
    );
}

// Test 4: Malformed XML - invalid XML declaration
BOOST_AUTO_TEST_CASE(MalformedXmlDeclaration) {
    std::string xml = R"(<?xml version="2.0"?>
<methodResponse>
  <params>
    <param>
      <value><string>test</string></value>
    </param>
  </params>
</methodResponse>)";

    // Should still parse (XML 1.0 is backward compatible)
    auto result = parse_string_response(xml);
    BOOST_CHECK_EQUAL(result, "test");
}

// Test 5: Missing methodResponse element
BOOST_AUTO_TEST_CASE(MissingMethodResponse) {
    std::string xml = R"(<?xml version="1.0"?>
<params>
  <param>
    <value><string>test</string></value>
  </param>
</params>)";

    BOOST_CHECK_THROW(
        parse_string_response(xml),
        std::exception
    );
}

// Test 6: Missing params element
BOOST_AUTO_TEST_CASE(MissingParams) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <value><string>test</string></value>
</methodResponse>)";

    BOOST_CHECK_THROW(
        parse_string_response(xml),
        std::exception
    );
}

// Test 7: Empty response
BOOST_AUTO_TEST_CASE(EmptyResponse) {
    std::string xml = "";

    BOOST_CHECK_THROW(
        parse_string_response(xml),
        std::exception
    );
}

// Test 8: Just XML declaration
BOOST_AUTO_TEST_CASE(OnlyXmlDeclaration) {
    std::string xml = R"(<?xml version="1.0"?>)";

    BOOST_CHECK_THROW(
        parse_string_response(xml),
        std::exception
    );
}

// Test 9: Invalid boolean value
BOOST_AUTO_TEST_CASE(InvalidBooleanValue) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value><boolean>maybe</boolean></value>
    </param>
  </params>
</methodResponse>)";

    BOOST_CHECK_THROW(
        parse_boolean_response(xml),
        std::exception
    );
}

// Test 10: Valid boolean values
BOOST_AUTO_TEST_CASE(ValidBooleanValues) {
    std::string xml_true = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value><boolean>1</boolean></value>
    </param>
  </params>
</methodResponse>)";

    std::string xml_false = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value><boolean>0</boolean></value>
    </param>
  </params>
</methodResponse>)";

    BOOST_CHECK_EQUAL(parse_boolean_response(xml_true), true);
    BOOST_CHECK_EQUAL(parse_boolean_response(xml_false), false);
}

// Test 11: Missing struct members
BOOST_AUTO_TEST_CASE(MissingStructMembers) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test_proc</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "test_proc");
    // When a member is missing entirely, we get empty string (not the default "UNKNOWN")
    // because the struct member exists but the value.string path returns ""
    BOOST_CHECK_EQUAL(processes[0].statename, "");
    BOOST_CHECK_EQUAL(processes[0].pid, -1); // Default
}

// Test 12: Empty string values
BOOST_AUTO_TEST_CASE(EmptyStringValues) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string></string></value></member>
                <member><name>statename</name><value><string>STOPPED</string></value></member>
                <member><name>description</name><value><string></string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "");
    BOOST_CHECK_EQUAL(processes[0].statename, "STOPPED");
    BOOST_CHECK_EQUAL(processes[0].description, "");
}

// Test 13: Special characters in strings
BOOST_AUTO_TEST_CASE(SpecialCharactersInStrings) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test&amp;proc</string></value></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
                <member><name>description</name><value><string>&lt;output&gt;</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "test&proc");
    BOOST_CHECK_EQUAL(processes[0].description, "<output>");
}

// Test 14: Multiple processes in array
BOOST_AUTO_TEST_CASE(MultipleProcesses) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>proc1</string></value></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
              </struct>
            </value>
            <value>
              <struct>
                <member><name>name</name><value><string>proc2</string></value></member>
                <member><name>statename</name><value><string>STOPPED</string></value></member>
              </struct>
            </value>
            <value>
              <struct>
                <member><name>name</name><value><string>proc3</string></value></member>
                <member><name>statename</name><value><string>FATAL</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 3);
    BOOST_CHECK_EQUAL(processes[0].name, "proc1");
    BOOST_CHECK_EQUAL(processes[1].name, "proc2");
    BOOST_CHECK_EQUAL(processes[2].name, "proc3");
    BOOST_CHECK_EQUAL(processes[0].statename, "RUNNING");
    BOOST_CHECK_EQUAL(processes[1].statename, "STOPPED");
    BOOST_CHECK_EQUAL(processes[2].statename, "FATAL");
}

// Test 15: Very long string values
BOOST_AUTO_TEST_CASE(VeryLongStringValues) {
    std::string long_desc(10000, 'x');
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test</string></value></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
                <member><name>description</name><value><string>)" + long_desc + R"(</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].description.length(), 10000);
}

// Test 16: Nested arrays (invalid for our use case)
BOOST_AUTO_TEST_CASE(NestedArrays) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <array>
                <data>
                  <value><string>nested</string></value>
                </data>
              </array>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    // Parser creates an entry for each <value> even if it doesn't contain a struct
    // The ProcessInfo will have default values
    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, ""); // Empty/default
    BOOST_CHECK_EQUAL(processes[0].pid, -1); // Default
}

// Test 17: Wrong data type for integer field
BOOST_AUTO_TEST_CASE(WrongDataTypeForInteger) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test</string></value></member>
                <member><name>pid</name><value><string>not_an_int</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    // Parser uses default value when type doesn't match
    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].pid, -1); // Default value
}

// Test 18: Missing value element in member
BOOST_AUTO_TEST_CASE(MissingValueInMember) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    // Should parse with default/empty values
    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "");
    BOOST_CHECK_EQUAL(processes[0].statename, "RUNNING");
}

// Test 19: Unicode characters in strings
BOOST_AUTO_TEST_CASE(UnicodeCharactersInStrings) {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test_程序</string></value></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
                <member><name>description</name><value><string>Процесс работает 🚀</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK(processes[0].name.find("test_") != std::string::npos);
}

// Test 20: Whitespace handling in XML
BOOST_AUTO_TEST_CASE(WhitespaceHandling) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member>
                  <name>name</name>
                  <value>
                    <string>  test_with_spaces  </string>
                  </value>
                </member>
                <member>
                  <name>statename</name>
                  <value>
                    <string>RUNNING</string>
                  </value>
                </member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    // Boost PropertyTree preserves whitespace in text content
    BOOST_CHECK_EQUAL(processes[0].name, "  test_with_spaces  ");
}

// Test 21: CDATA sections
BOOST_AUTO_TEST_CASE(CdataSections) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <string><![CDATA[This is <some> special & text]]></string>
      </value>
    </param>
  </params>
</methodResponse>)";

    auto result = parse_string_response(xml);
    BOOST_CHECK_EQUAL(result, "This is <some> special & text");
}

// Test 22: Mixed content in struct (unexpected elements)
BOOST_AUTO_TEST_CASE(MixedContentInStruct) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test</string></value></member>
                <unexpected>unexpected element</unexpected>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    // Should parse successfully, ignoring unexpected elements
    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "test");
    BOOST_CHECK_EQUAL(processes[0].statename, "RUNNING");
}

// Test 23: Truncated XML
BOOST_AUTO_TEST_CASE(TruncatedXml) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>test)";

    BOOST_CHECK_THROW(
        parse_process_info_array(xml),
        std::exception
    );
}

// Test 24: Duplicate member names in struct
BOOST_AUTO_TEST_CASE(DuplicateMemberNames) {
    std::string xml = R"(<?xml version="1.0"?>
<methodResponse>
  <params>
    <param>
      <value>
        <array>
          <data>
            <value>
              <struct>
                <member><name>name</name><value><string>first</string></value></member>
                <member><name>name</name><value><string>second</string></value></member>
                <member><name>statename</name><value><string>RUNNING</string></value></member>
              </struct>
            </value>
          </data>
        </array>
      </value>
    </param>
  </params>
</methodResponse>)";

    // Parser processes both, last value wins
    auto processes = parse_process_info_array(xml);
    BOOST_CHECK_EQUAL(processes.size(), 1);
    BOOST_CHECK_EQUAL(processes[0].name, "second");
}

BOOST_AUTO_TEST_SUITE_END()
