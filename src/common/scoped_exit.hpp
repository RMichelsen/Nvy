#pragma once

#include <exception>
#include <type_traits>
#include <utility>

// Let us say that our F can throw
// Then in that case, our code will check for uncaught exceptions
// However, if you are sure outer scope will not throw at all
// Then you can reduce the cost of the check
auto inline constexpr dont_check_for_uncaught_exceptions = std::false_type{};

template <typename F, bool checkForUncaughtExceptions = true>
// Change Requires to an ugly enable_if_t or a static_assert for C++17 support
requires(std::is_invocable_v<F>) struct scoped_exit
{
    // Store the function only
    // No need to store a 2nd variable
    F const f;

    constexpr explicit scoped_exit(F &&f,
        // By default check for uncaught exceptions
        // If the Cleanup function is called during
        // An uncaught exception
        // This would ensure that the cleanup is not run
        // However, in case we wish to disable this check at compile-time
        // We can
        const std::bool_constant<checkForUncaughtExceptions> = std::true_type{}) noexcept
        // If the function is nothrow invocable
        // Then modifying the checkForUncaughtExceptions is useless
        // So don't let user modify the param
        requires(!(!checkForUncaughtExceptions && std::is_nothrow_invocable_v<F>))
        : f(std::forward<F>(f))
    {
    }

    ~scoped_exit() noexcept(
        // GSL Destructors are always noexcept
        // But sometimes, we might have to throw an exception
        // It is a convenience factor
        std::is_nothrow_invocable_v<F>)
    {
        // Check if can be invoked without throwing
        // Or user is sure, the chances of a double throw are low
        // Risky. As such checkForUncaughtExceptions is set to true by default
        if constexpr (std::is_nothrow_invocable_v<F> || !checkForUncaughtExceptions)
            f();
        else
            // Run cleanup only when there are no uncaught_exceptions
            if (std::uncaught_exceptions() == 0) [[likely]]
            f();
    }

    // Disable move & copy
    scoped_exit(const scoped_exit &) = delete;
    scoped_exit(scoped_exit &&) = delete;
    scoped_exit &operator=(scoped_exit &&) = delete;
    scoped_exit &operator=(const scoped_exit &) = delete;
};

// Ensure naming convention matches GSL
// To make switching from GSL to pc::scoped_exit easy
template <typename F>
using finally = scoped_exit<F>;
template <typename F>
using final_action = scoped_exit<F>;