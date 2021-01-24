#include <c++utilities/tests/testutils.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;

class PacfindTests : public TestFixture {
    CPPUNIT_TEST_SUITE(PacfindTests);
    CPPUNIT_TEST(test);
    CPPUNIT_TEST_SUITE_END();

public:
    PacfindTests();
    void setUp() override;
    void tearDown() override;

    void test();
};

CPPUNIT_TEST_SUITE_REGISTRATION(PacfindTests);

PacfindTests::PacfindTests()
{
}

void PacfindTests::setUp()
{
}

void PacfindTests::tearDown()
{
}

void PacfindTests::test()
{
}
