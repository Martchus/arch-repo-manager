#include <c++utilities/tests/testutils.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;

class PacParseTests : public TestFixture {
    CPPUNIT_TEST_SUITE(PacParseTests);
    CPPUNIT_TEST(test);
    CPPUNIT_TEST_SUITE_END();

public:
    PacParseTests();
    void setUp() override;
    void tearDown() override;

    void test();
};

CPPUNIT_TEST_SUITE_REGISTRATION(PacParseTests);

PacParseTests::PacParseTests()
{
}

void PacParseTests::setUp()
{
}

void PacParseTests::tearDown()
{
}

void PacParseTests::test()
{
}
