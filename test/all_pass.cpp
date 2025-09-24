#define EM_ENABLE_TESTS
#define EM_MINITEST_IMPLEMENTATION
#include <em/minitest.hpp>

EM_MINITEST_MAIN

EM_TEST( foo ) {}
EM_TEST( bar ) { EM_CHECK(true); }
EM_TEST( hello ) { EM_CHECK(10 < 20); }
