#define EM_ENABLE_TESTS
#define EM_MINITEST_IMPLEMENTATION
#include <em/minitest.hpp>

EM_MINITEST_MAIN

#include <chrono>
#include <thread>

EM_TEST( pass ) {}

EM_TEST( pass2 )
{
    EM_CHECK(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::printf("Hello!\n");
}

#if __cpp_exceptions
EM_TEST( throw_simple )
{
    throw std::runtime_error("heh");
}

EM_TEST( throw_nested )
{
    try
    {
        try
        {
            throw std::runtime_error("heh");
        }
        catch (...)
        {
            std::throw_with_nested(std::out_of_range("while doing more stuff:"));
        }
    }
    catch (...)
    {
        std::throw_with_nested(std::logic_error("while doing stuff:"));
    }
}

EM_TEST( throw_unknown )
{
    throw 42;
}

EM_TEST( throw_nested_unknown )
{
    try
    {
        throw 42;
    }
    catch (...)
    {
        std::throw_with_nested(std::logic_error("while doing stuff:"));
    }
}
#endif

EM_TEST( assert_false )
{
    std::printf("Before!\n");
    EM_CHECK_SOFT(false);
    std::printf("After soft assertion!\n");
    EM_CHECK(false);
    std::printf("After hard assertion!\n");
}

#if __cpp_exceptions
EM_TEST( assert_throws )
{
    std::printf("Before!\n");
    EM_CHECK_SOFT(throw std::runtime_error("huh"), true);
    std::printf("After soft assertion!\n");
    EM_CHECK(throw std::runtime_error("huh"), true);
    std::printf("After hard assertion!\n");
}

EM_TEST( assert_throws_unknown )
{
    EM_CHECK(throw 42, true);
}
#endif
