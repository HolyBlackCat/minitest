#define EM_ENABLE_TESTS
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
            // Test multiline errors, while we're at it.
            throw std::runtime_error("heh\nthis is a\nmultiline message");
        }
        catch (...)
        {
            std::throw_with_nested(std::out_of_range("while doing more stuff:\n(and another line)"));
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

EM_TEST( throw_interrupt )
{
    // This doesn't fail the test.
    EM_CHECK(throw em::minitest::InterruptTestException{}, true);
}
#endif

#define FALSEY false

EM_TEST( assert_false )
{
    std::printf("Before!\n");
    EM_CHECK_SOFT(FALSEY); // `FALSEY` shouldn't be expanded in the log!
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

// Expected any exception, got none.
EM_TEST( must_throw_any_fail )
{
    std::printf("Before!\n");
    EM_MUST_THROW_SOFT( 42 );
    std::printf("After soft check!\n");
    EM_MUST_THROW( 42 );
    std::printf("After hard check!\n");
}

// Expected specific exception, got none.
EM_TEST( must_throw_fail )
{
    std::printf("Before!\n");
    EM_MUST_THROW_SOFT( 42 )( std::runtime_error("a") );
    std::printf("After soft check!\n");
    EM_MUST_THROW( 42 )( std::runtime_error("a"), std::logic_error("b") );
    std::printf("After hard check!\n");
}

// Expected any exception, got unknown.
EM_TEST( must_throw_any_unknown )
{
    EM_MUST_THROW( throw 42 );
}

// Expected any exception, got known.
EM_TEST( must_throw_any )
{
    EM_MUST_THROW( throw std::runtime_error("foo") );
}

// Expected specific exception, got unknown.
EM_TEST( must_throw_mismatch_unknown )
{
    std::printf("Before!\n");
    EM_MUST_THROW_SOFT( throw 42 )( std::runtime_error("42") );
    std::printf("After soft check!\n");
    EM_MUST_THROW( throw 42 )( std::runtime_error("42\nhello world") );
    std::printf("After hard check!\n");
}

// Expected specific exception, got wrong type.
EM_TEST( must_throw_mismatch_type )
{
    std::printf("Before!\n");
    EM_MUST_THROW_SOFT( throw std::logic_error("foo\nbarbar") )( std::runtime_error("foo\nbarbar") );
    std::printf("After soft check!\n");
    EM_MUST_THROW( throw std::logic_error("some long long message\nbarbar") )( std::runtime_error("some long long message\nbarbar") );
    std::printf("After hard check!\n");
}

// Expected specific exception, got wrong message. (Also mismatching the type to avoid the shorter printing format that's used for mismatched messages only.)
EM_TEST( must_throw_mismatch_message )
{
    std::printf("Before!\n");
    EM_MUST_THROW_SOFT( throw std::logic_error("foo\nbarbar1") )( std::runtime_error("foo\nbarbar") );
    // Too many lines in the actual exception.
    EM_MUST_THROW_SOFT( throw std::logic_error("foo\nbarbar\nhmm") )( std::runtime_error("foo\nbarbar") );
    // Too many lines in the expected exception.
    EM_MUST_THROW_SOFT( throw std::logic_error("foo\nbarbar") )( std::runtime_error("foo\nbarbar\nhmm") );
    std::printf("After soft check!\n");
    EM_MUST_THROW( throw std::logic_error("some long long message1\nbarbar") )( std::runtime_error("some long long message\nbarbar") );
    std::printf("After hard check!\n");
}

// Expected specific exception, got wrong message and the same type. This uses a nicer printing format.
EM_TEST( must_throw_mismatch_message_only )
{
    EM_MUST_THROW_SOFT( throw std::runtime_error("blah") )( std::runtime_error("bleh") );
    EM_MUST_THROW_SOFT( throw std::runtime_error("blah\nfoo") )( std::runtime_error("bleh") );
    EM_MUST_THROW_SOFT( throw std::runtime_error("blah") )( std::runtime_error("bleh\nfoo") );
}

// Problems in nested exceptions
EM_TEST( must_throw_mismatch_nested )
{
    // More nesting than expected.
    EM_MUST_THROW_SOFT(
        try
        {
            throw std::runtime_error("blah");
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("logic"));
        }
    )( std::logic_error("logic") );

    // More nesting than expected, with an unknown exception.
    EM_MUST_THROW_SOFT(
        try
        {
            throw 42;
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("logic"));
        }
    )( std::logic_error("logic") );

    // Less nesting than expected.
    EM_MUST_THROW_SOFT(
        throw std::runtime_error("blah");
    )( std::runtime_error("blah"), std::runtime_error("bleh") );

    // Message mismatch with nesting involved. To test that it doesn't use the shorter printing format because of nesting.
    EM_MUST_THROW_SOFT(
        try
        {
            throw std::runtime_error("blah");
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("logic1"));
        }
    )( std::logic_error("logic"), std::runtime_error("blah") );
}

EM_TEST( must_throw_pass_nested )
{
    // More nesting than expected.
    EM_MUST_THROW_SOFT(
        try
        {
            throw std::runtime_error("blah");
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("logic"));
        }
    )( std::logic_error("logic"), std::runtime_error("blah") );
}
#endif


// Test `EM_TRY()`:

#if EM_MINITEST_EXCEPTIONS
EM_TEST( try )
{
    EM_TRY( 1 + 1 );
    EM_TRY_SOFT( 1 + 1; );

    EM_TRY_SOFT( throw std::runtime_error("1") );
    EM_TRY_SOFT( throw std::runtime_error("2"); );
    EM_TRY( throw std::runtime_error("3"); );
    EM_TRY( throw std::runtime_error("4"); );
}
#endif

EM_TEST( try_success )
{
    EM_TRY( 1 + 1 );
    EM_TRY_SOFT( 1 + 1; );
}
