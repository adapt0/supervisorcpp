#define BOOST_TEST_MODULE XmlRpcTest
#include <boost/test/unit_test.hpp>
#include "rpc/xmlrpc.h"

namespace xmlrpc = supervisorcpp::xmlrpc;

BOOST_AUTO_TEST_CASE(xmlrpc__value) {
    BOOST_CHECK_EQUAL(xmlrpc::Value{"Test"}.toString(), "<string>Test</string>");

    BOOST_CHECK_EQUAL(xmlrpc::Value{42}.toString(), "<int>42</int>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{-2}.toString(), "<int>-2</int>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{0}.toString(), "<int>0</int>");

    BOOST_CHECK_EQUAL(xmlrpc::Value{true}.toString(), "<boolean>1</boolean>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{false}.toString(), "<boolean>0</boolean>");

    BOOST_CHECK_EQUAL(xmlrpc::Member("ABoolean", true).toString(), "<member><name>ABoolean</name><value><boolean>1</boolean></value></member>");
    BOOST_CHECK_EQUAL(xmlrpc::Member("Beg<>End", "Beg & End").toString(), "<member><name>Beg&lt;&gt;End</name><value><string>Beg &amp; End</string></value></member>");
}
