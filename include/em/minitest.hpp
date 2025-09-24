#pragma once

#ifndef EM_ENABLE_TESTS
#error Must define `EM_ENABLE_TESTS` when including `em/minitest.hpp`. Wrap your tests in `#if EM_ENABLE_TESTS` or don't compile their source files in non-test projects.
#endif

// The export macro.
#ifndef EM_MINITEST_API
#  ifdef _WIN32
#    ifdef EM_MINITEST_IMPLEMENTATION
#      define EM_MINITEST_API __declspec(dllexport)
#    else
#      define EM_MINITEST_API __declspec(dllimport)
#    endif
#  else
#    define EM_MINITEST_API __attribute__((__visibility__("default")))
#  endif
#endif

#ifndef EM_MINITEST_EXCEPTIONS
#  ifdef __cpp_exceptions
#    define EM_MINITEST_EXCEPTIONS 1
#  else
#    define EM_MINITEST_EXCEPTIONS 0
#  endif
#endif

#include <compare> // IWYU pragma: keep, we default `operator<=>` below.
#include <cstddef>
#include <cstdio>
#include <exception>
#include <functional>
#include <map>
#include <string_view>
#include <string>
#include <type_traits>
#include <typeinfo> // IWYU pragma: keep, we use `typeid()` below.
#include <utility>

namespace em::minitest
{
    #if EM_MINITEST_EXCEPTIONS
    // Throw this to stop the current test early. Doesn't affect the pass/fail status.
    // Intentionally don't inherit from `std::exception` to indicate that this is not an error, and to discourage people from catching it.
    struct InterruptTestException {};
    #endif

    // Runs all tests. Returns the exit code, `0` if everything passes.
    [[nodiscard]] EM_MINITEST_API int RunTests(int argc, char **argv);

    namespace detail
    {
        // Terminates the program with an error.
        [[noreturn]] EM_MINITEST_API void InternalError(std::string_view message);

        struct TestDesc
        {
            std::string_view file; // Null-terminated.
            int line = 0;
            std::string_view name; // Null-terminated.

            friend auto operator<=>(const TestDesc &, const TestDesc &) = default;
        };

        // Describes a known test.
        struct Test
        {
            void (*func)() = nullptr;
        };

        // Using `std::map` to sort by filename.
        using TestMap = std::map<TestDesc, Test>;
        // This singleton stores all known tests.
        [[nodiscard]] EM_MINITEST_API TestMap &GetTestMap();

        // A compile-time string.
        template <std::size_t N>
        struct ConstString
        {
            char str[N]{};

            static constexpr std::size_t size = N - 1;

            consteval ConstString() {}
            consteval ConstString(const char (&new_str)[N])
            {
                if (new_str[N-1] != '\0')
                    std::unreachable();

                // Hopefully a manual loop compiles faster than `std::copy_n()`.
                for (std::size_t i = 0; i < size; i++)
                    str[i] = new_str[i];
            }

            [[nodiscard]] constexpr std::string_view view() const &
            {
                return std::string_view(str, str + size);
            }
            [[nodiscard]] constexpr std::string_view view() const && = delete;
        };

        // This is used to register tests.
        template <ConstString File, int Line, ConstString Name>
        struct ConstTestDesc
        {
            // The function pointer is kept in separate template parameters, because we use the type of `ConstTestDesc` to detect
            //   multiple definitions of tests at link time, and the pointer would be always unique, and would prevent this.
            template <void (*F)()>
            inline static const ConstTestDesc register_test = []{
                TestMap &m = GetTestMap();

                auto [iter, is_new] = m.try_emplace(TestDesc{.file = File.view(), .line = Line, .name = Name.view()});
                // Here duplicates shouldn't be possible, since they should cause link errors.
                if (!is_new)
                    detail::InternalError("A duplicate test was registered at `" + std::string(File.view()) + ":" + std::to_string(Line) + "`, named `" + std::string(Name.view()) + "`.");

                iter->second.func = F;

                return ConstTestDesc{};
            }();
        };

        // A simple imitation of `std::function_ref`.
        template <typename F>
        class FuncRef;
        template <typename R, typename ...P>
        class FuncRef<R(P...)>
        {
            R (*func)(void *data, P...) = nullptr;
            void *data = nullptr;

          public:
            template <typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, FuncRef> && std::is_invocable_r_v<R, F &&, P &&...>)
            constexpr FuncRef(F &&f)
                : func([](void *data, P ...params) -> R {return std::invoke(*(std::remove_reference_t<F> *)data, std::forward<P>(params)...);}),
                data(const_cast<void *>(static_cast<const void *>(&f))) // Must manually drop constness here.
            {}

            [[nodiscard]] constexpr R operator()(P ...params) const
            {
                return func(data, std::forward<P>(params)...);
            }
        };

        #if EM_MINITEST_EXCEPTIONS
        // Calls `on_element` for the current exception and each nested exception. Always calls it at least once.
        // If the callback returns true, stops the function and also returns true.
        // The last call can receive an unknown exception, then you'll receive `type_name == ""` and `message == nullptr`.
        // The type is passed as a string because we can't get the current `std::type_index` for nested exception (the current type, not the nested one).
        EM_MINITEST_API bool AnalyzeCurrentException(FuncRef<bool(std::string_view type_name, const char *message)> on_element);
        // Uses `AnalyzeCurrentException()` to print the current exception.
        EM_MINITEST_API void PrintCurrentException(const char *indent);
        #endif

        #if EM_MINITEST_EXCEPTIONS
        // Don't call this directly, use `DETAIL_EM_MINITEST_RUN_WITH_CATCH()`.
        // Runs the lambda once. If it doesn't throw, returns its return value.
        // If the lambda throws `InterruptTestException`, either rethrows it if `rethrow_interrupt`, or returns false otherwise.
        // If the lambda throws something else, calls `on_failure()` and returns false.
        // You must manually fail the test in `on_failure`, manually print the exception if needed, and manually throw `InterruptTestException` if needed.
        EM_MINITEST_API bool RunWithCatchImpl(bool rethrow_interrupt, FuncRef<bool()> func, FuncRef<void()> on_failure);
        #define DETAIL_EM_MINITEST_RUN_WITH_CATCH(rethrow_interrupt_, func_, .../*on_failure*/) detail::RunWithCatchImpl((rethrow_interrupt_), (func_), __VA_ARGS__)
        #else
        #define DETAIL_EM_MINITEST_RUN_WITH_CATCH(rethrow_interrupt_, func_, .../*on_failure*/) (func_)()
        #endif

        // Do an assertion. This is what `EM_CHECK(...)` calls.
        // `file` and `line` is the source location.
        // `expr_str` is the stringized input expression.
        EM_MINITEST_API bool Assert(bool stop_on_failure, const char *file, int line, const char *expr_str, FuncRef<bool()> func);
    }
}

#ifdef EM_MINITEST_IMPLEMENTATION
#include <chrono>

// Demangler dependencies:
#ifndef _MSC_VER
#include <cxxabi.h>
#include <cstdlib> // For `std::free`.
#endif

#define DETAIL_EM_MINITEST_LOG_STR "%*s [   .    ] "
#define DETAIL_EM_MINITEST_LOG_PARAMS (int)detail::test_counters_width, ".       " // Same width as `"0 failed"`.

namespace em::minitest
{
    namespace detail
    {
        namespace
        {
            class Demangler
            {
                #ifndef _MSC_VER
                // We keep those here so that `abi::__cxa_demangle()` can reuse its buffer between calls.
                char *buf_ptr = nullptr;
                std::size_t buf_size = 0;
                #endif

              public:
                Demangler() {}
                Demangler(const Demangler &) = delete;
                Demangler &operator=(const Demangler &) = delete;
                ~Demangler()
                {
                    #ifndef _MSC_VER
                    std::free(buf_ptr); // Does nothing if `buf_ptr` is null.
                    #endif
                }

                // Demangles the `typeid(...).name()` if necessary, or returns `name` as is if this platform doesn't mangle the type names.
                // The resulting pointer is either owned by this `Demangler` instance (and is reused on the next call), or is just `name` as is.
                // So for portable use ensure that both the `Demangler` instance and the `name` stay alive as long as you use the resulting pointer.
                // Returns null on demangling failure, but I've never seen that happen.
                [[nodiscard]] const char *operator()(const char *name)
                {
                    #ifdef _MSC_VER
                    return name;
                    #else
                    int status = -4; // Some custom error code, in case `abi::__cxa_demangle()` doesn't modify it at all for some reason.
                    buf_ptr = abi::__cxa_demangle(name, buf_ptr, &buf_size, &status);
                    if (status != 0) // -1 = out of memory, -2 = invalid string, -3 = invalid usage
                        return nullptr; // Unable to demangle.
                    return buf_ptr;
                    #endif
                }
            };
        }

        static thread_local bool *fail_test_ptr = nullptr;

        static thread_local std::size_t test_counters_width = 0;

        void InternalError(std::string_view message)
        {
            std::fprintf(stderr, "minitest: Internal error: %.*s\n", (int)message.size(), message.data());
            std::exit(2); // Because `1` is for failed tests.
        }

        [[nodiscard]] TestMap &GetTestMap()
        {
            static TestMap ret;
            return ret;
        }

        #if EM_MINITEST_EXCEPTIONS
        bool AnalyzeCurrentException(FuncRef<bool(std::string_view type_name, const char *message)> on_element)
        {
            try
            {
                throw;
            }
            catch (std::exception &e)
            {
                Demangler d;
                std::string_view type_name = d(typeid(e).name());

                #define DETAIL_EM_MINITEST_EAT_PREFIX(prefix) \
                    if (type_name.starts_with(prefix)) \
                    { \
                        type_name.remove_prefix(sizeof(prefix) - 1); /* Minus the null terminator. Here we also trim the `<` which is included in the `prefix`. */ \
                        type_name.remove_suffix(1); /* Remove the `>`. */ \
                    }

                #ifdef _MSC_VER // MSVC or Clang in MSVC-compatible mode.
                #define DETAIL_EM_MINITEST_STRUCT_PREFIX "struct "
                #else
                #define DETAIL_EM_MINITEST_STRUCT_PREFIX
                #endif

                #if defined(_GLIBCXX_RELEASE)
                DETAIL_EM_MINITEST_EAT_PREFIX("std::_Nested_exception<") // This never gets used in MSVC-compatible mode anyway.
                #elif defined(_LIBCPP_VERSION)
                DETAIL_EM_MINITEST_EAT_PREFIX(DETAIL_EM_MINITEST_STRUCT_PREFIX "std::__nested<")
                #elif defined(_MSVC_STL_VERSION)
                DETAIL_EM_MINITEST_EAT_PREFIX(DETAIL_EM_MINITEST_STRUCT_PREFIX "std::_With_nested_v2<")
                #endif
                #undef DETAIL_EM_MINITEST_EAT_PREFIX
                #undef DETAIL_EM_MINITEST_STRUCT_PREFIX

                if (on_element(type_name, e.what()))
                    return true;

                try
                {
                    std::rethrow_if_nested(e);
                }
                catch (...)
                {
                    AnalyzeCurrentException(on_element);
                }
            }
            catch (...)
            {
                return on_element({}, nullptr);
            }

            return false;
        }

        void PrintCurrentException(const char *indent)
        {
            AnalyzeCurrentException([&](std::string_view type_name, const char *message)
            {
                if (type_name.empty())
                {
                    // Unknown type.
                    std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "%sUnknown exception.\n", DETAIL_EM_MINITEST_LOG_PARAMS, indent);
                }
                else
                {
                    // Print the known type.
                    // Here we don't print any special indication to distinguish from `Unknown exception.`, because that's clearly not a valid type anyway.
                    std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "%s%.*s\n", DETAIL_EM_MINITEST_LOG_PARAMS, indent, (int)type_name.size(), type_name.data());

                    // Print message.
                    // Here we don't print any special indication to distinguish from `(null)` simply because it looks better, and null shouldn't be possible here.
                    if (message)
                        std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "%s    %s\n", DETAIL_EM_MINITEST_LOG_PARAMS, indent, message);
                    else
                        std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "%s    (null)\n", DETAIL_EM_MINITEST_LOG_PARAMS, indent);
                }

                return false;
            });
        }

        bool RunWithCatchImpl(bool rethrow_interrupt, FuncRef<bool()> func, FuncRef<void()> on_failure)
        {
            // Run the test with exception catching.
            try
            {
                return func();
            }
            catch (InterruptTestException)
            {
                if (rethrow_interrupt)
                    throw;
                else
                    return false;
            }
            catch (...)
            {
                on_failure();
                return false;
            }
        }
        #endif

        bool Assert(bool stop_on_failure, const char *file, int line, const char *expr_str, FuncRef<bool()> func)
        {
            #if EM_MINITEST_EXCEPTIONS
            bool got_exception = false;
            #endif

            auto FailAssert = [&]
            {
                // Flush the user output.
                std::fflush(stdout);
                std::fflush(stderr);

                *fail_test_ptr = true;
                // It should be impossible for this to be called twice, so there is no guard.
                std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "    Assertion failed at:  %s:%d\n", DETAIL_EM_MINITEST_LOG_PARAMS, file, line);
                std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "        Expression:  %s\n", DETAIL_EM_MINITEST_LOG_PARAMS, expr_str);

                #if EM_MINITEST_EXCEPTIONS
                if (got_exception)
                {
                    std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "        Threw an uncaught exception:\n", DETAIL_EM_MINITEST_LOG_PARAMS);
                    detail::PrintCurrentException("            ");
                }
                else
                #endif
                {
                    std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "        Evaluated to false.\n", DETAIL_EM_MINITEST_LOG_PARAMS);
                }

                #if EM_MINITEST_EXCEPTIONS
                if (stop_on_failure)
                    throw InterruptTestException{};
                #else
                (void)stop_on_failure;
                #endif
            };

            bool ret = DETAIL_EM_MINITEST_RUN_WITH_CATCH(true, func, [&]
            {
                got_exception = true;
                FailAssert(); // This can't be deduplicated with the one below, because this one is in a `catch`, and rethrows the current exception to print it.
            });

            if (!ret
                #if EM_MINITEST_EXCEPTIONS
                && !got_exception
                #endif
            )
                FailAssert();

            return ret;
        }
    }

    int RunTests(int argc, char **argv)
    {
        (void)argc;
        (void)argv;

        std::string_view cur_file;

        const auto &test_map = detail::GetTestMap();

        if (test_map.empty())
        {
            std::fprintf(stderr, "minitest: No tests to run.\n");
            return 1; // For now this is an error. It should probably be allowed if caused by filtering (which we don't have yet).
        }

        std::size_t num_tests_ran = 0; // The loop gradually increments this, so at the end this will match `num_tests_total`.
        std::size_t num_tests_total = test_map.size();

        std::vector<const std::remove_cvref_t<decltype(test_map)>::value_type *> failed_tests;
        std::size_t failed_tests_max_name_len = 0;

        // We need this much whitespace: "  0 failed"
        std::string str_failed_counter = "          ";

        // Run the tests.
        for (const auto &elem : test_map)
        {
            num_tests_ran++; // Increment this before logging.

            bool fail_test = false;

            using Clock = std::chrono::steady_clock;
            Clock::time_point test_start_time; // This gets set later, right before running the test.
            Clock::time_point test_end_time; // This gets set later, right after running the test.

            std::string str_test_counters = std::to_string(num_tests_ran) + "/" + std::to_string(num_tests_total);

            auto LogPrePostRunTest = [&](bool post)
            {
                // This should be first.
                // After the test, flush all the user streams.
                // If we don't do this, then the output isn't interleaved correctly when mixing stdout and stderr (even on pure C streams),
                //   when both streams are redirected to the same file.
                if (post)
                {
                    std::fflush(stdout);
                    std::fflush(stderr);
                }

                // Update test counters width.
                detail::test_counters_width = std::max(str_test_counters.size(), str_failed_counter.size());

                // Are we switching to a different file?
                if (cur_file != elem.first.file)
                {
                    cur_file = elem.first.file;
                    for (std::size_t i = 0; i < detail::test_counters_width; i++)
                        std::fputc('#', stderr);
                    std::fprintf(stderr, " [ file   ] --- %s\n", cur_file.data()); // This is guaranteed to be null-terminated.
                }

                // Test counters.
                std::fprintf(stderr, "%-*s", (int)detail::test_counters_width, post ? str_failed_counter.c_str() : str_test_counters.c_str());

                // Explain what we're doing with this test.
                std::fprintf(stderr, " %s", !post ? "[ run    ]" : fail_test ? "[   FAIL ]" : "[     OK ]");

                // Test name.
                std::fprintf(stderr, " %s", elem.first.name.data()); // This is always null-terminated.

                // Print the elapsed time.
                if (post)
                {
                    auto t = std::chrono::duration_cast<std::chrono::microseconds>(test_end_time - test_start_time).count();
                    std::fprintf(stderr, " (%.1f ms)", t / 1000.0);
                }

                // Print the source location of failed tests.
                if (post && fail_test)
                    std::fprintf(stderr, "   at:  %s:%d", elem.first.file.data(), elem.first.line); // `elem.first.file` is always null-terminated.

                std::fputc('\n', stderr);

                // This should be last.
                // Flush stderr before running the user test. Our framework doesn't write to `stdout` (only the user can), so that doesn't need to be flushed.
                // See the beginning of this function for more details.
                if (post)
                    std::fflush(stderr);
            };

            // Log pre run test.
            LogPrePostRunTest(false);

            { // Run the test. Here we need a scope for RAII purposes.
                // Register the test pass flag into the thread-local singleton.
                detail::fail_test_ptr = &fail_test;
                struct Guard
                {
                    ~Guard()
                    {
                        detail::fail_test_ptr = nullptr;
                    }
                };
                Guard guard;

                // Begin measuring time.
                test_start_time = Clock::now();

                // Run the test.
                DETAIL_EM_MINITEST_RUN_WITH_CATCH(
                    false,
                    [&]
                    {
                        elem.second.func();
                        return false; // The return value doesn't matter.
                    },
                    [&]
                    {
                        fail_test = true;

                        // Flush the user output.
                        std::fflush(stdout);
                        std::fflush(stderr);

                        std::fprintf(stderr, DETAIL_EM_MINITEST_LOG_STR "    Uncaught exception:\n", DETAIL_EM_MINITEST_LOG_PARAMS);
                        detail::PrintCurrentException("        ");
                    }
                );
            }

            // Finish measuring time.
            test_end_time = Clock::now();

            // Did the test fail? Do this before logging to log the updated count.
            if (fail_test)
            {
                failed_tests.push_back(&elem);
                str_failed_counter = "  " + std::to_string(failed_tests.size()) + " failed";
                if (elem.first.name.size() > failed_tests_max_name_len)
                    failed_tests_max_name_len = elem.first.name.size();
            }

            // Log post run test.
            LogPrePostRunTest(true);
        }

        { // Log summary.
            if (failed_tests.empty())
            {
                std::fprintf(stderr, "\nAll %zu test%s passed\n", num_tests_total, num_tests_total == 1 ? "" : "s");
            }
            else
            {
                std::fprintf(stderr, "\nFailed tests:\n");
                for (const auto *elem : failed_tests)
                {
                    std::fprintf(stderr, "    %-*s   at:  %s:%d\n", (int)failed_tests_max_name_len, elem->first.name.data(), elem->first.file.data(), elem->first.line);
                }

                std::fprintf(stderr, "\nRan %zu test%s, %zu passed, %zu FAILED\n", num_tests_total, num_tests_total == 1 ? "" : "s", num_tests_total - failed_tests.size(), failed_tests.size());
            }
        }

        return failed_tests.empty() ? 0 : 1;
    }
}
#endif

// Defines the main function. This is optional, you can call `em::minitest::RunTests()` yourself.
#define EM_MINITEST_MAIN \
    int main(int argc, char **argv) {return ::em::minitest::RunTests(argc, argv);}

// Declare a test: `EM_TEST(identifier) {body...}`. Only usable in .cpp files. Trying to use those in headers will cause multiple definition errors.
#define EM_TEST(name) DETAIL_EM_MINITEST_TEST(name, DETAIL_EM_MINITEST_CAT(__test_,name))

// Evaluate an assertion: `EM_CHECK(cond)`. The condition doesn't have to be a boolean. Anything that `if (...)` accepts is fine.
// This version tries to stop the test on failure. This is only possible if exceptiosn are enabled.
#define EM_CHECK(...) ::em::minitest::detail::Assert(true, __FILE__, __LINE__, #__VA_ARGS__, [&] -> decltype(auto) {return (__VA_ARGS__);});
// Evaluate an assertion: `EM_CHECK(cond)`. The condition doesn't have to be a boolean. Anything that `if (...)` accepts is fine.
// This version doesn't try to stop the test on failure.
#define EM_CHECK_SOFT(...) ::em::minitest::detail::Assert(false, __FILE__, __LINE__, #__VA_ARGS__, [&] -> decltype(auto) {return (__VA_ARGS__);});

#define DETAIL_EM_MINITEST_TEST(name_, func_name_) \
    /* Make sure we're at namespace scope. */\
    namespace {} \
    static void func_name_(); \
    /* This is non-static to error on test definitions in headers (which aren't useful anyway, because in general a header might be included in no TUs). */\
    /* The different parameter types are used to make the tests with the same name but different locations not collide with each other. */\
    /* Note that the function pointer */\
    auto __em_register_test(::em::minitest::detail::ConstTestDesc<__FILE__, __LINE__, #name_> __em_desc) {return decltype(__em_desc)::register_test<func_name_>;}\
    /* This is static to allow different TUs to use the same test names. */\
    static void func_name_()

#define DETAIL_EM_MINITEST_CAT(x, y) DETAIL_EM_MINITEST_CAT_(x, y)
#define DETAIL_EM_MINITEST_CAT_(x, y) x##y

#define DETAIL_EM_MINITEST_STR(...) DETAIL_EM_MINITEST_STR_(__VA_ARGS__)
#define DETAIL_EM_MINITEST_STR_(...) #__VA_ARGS__
