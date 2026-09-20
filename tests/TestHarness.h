// Minimal assertion harness: assertions stay enabled in every build type.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace kcftest
{

struct Registry
{
    struct Case { std::string name; std::function<void()> body; };
    static std::vector<Case>& cases() { static std::vector<Case> c; return c; }
    static int& failures() { static int f = 0; return f; }
    static int& checks() { static int c = 0; return c; }
    static std::string& current() { static std::string n; return n; }
};

struct Registrar
{
    Registrar (const char* name, std::function<void()> body) { Registry::cases().push_back ({ name, std::move (body) }); }
};

inline void reportFailure (const char* file, int line, const std::string& what)
{
    ++Registry::failures();
    std::printf ("  FAIL [%s] %s:%d: %s\n", Registry::current().c_str(), file, line, what.c_str());
    std::fflush (stdout);
}

inline int runAll (int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (auto& c : Registry::cases())
    {
        if (! filter.empty() && c.name.find (filter) == std::string::npos)
            continue;
        Registry::current() = c.name;
        const int before = Registry::failures();
        std::printf ("- %s\n", c.name.c_str());
        std::fflush (stdout);
        try { c.body(); }
        catch (const std::exception& e) { reportFailure (__FILE__, __LINE__, std::string ("exception: ") + e.what()); }
        catch (...) { reportFailure (__FILE__, __LINE__, "unknown exception"); }
        ++ran;
        if (Registry::failures() != before)
            std::printf ("  -> FAILED\n");
    }
    std::printf ("\n%d test case(s), %d check(s), %d failure(s): %s\n", ran, Registry::checks(),
                 Registry::failures(), Registry::failures() == 0 ? "PASS" : "FAIL");
    return Registry::failures() == 0 ? 0 : 1;
}

} // namespace kcftest

#define KCF_CONCAT2(a, b) a##b
#define KCF_CONCAT(a, b) KCF_CONCAT2(a, b)
#define TEST_CASE(name) \
    static void KCF_CONCAT(kcfTestBody_, __LINE__)(); \
    static kcftest::Registrar KCF_CONCAT(kcfTestReg_, __LINE__) (name, &KCF_CONCAT(kcfTestBody_, __LINE__)); \
    static void KCF_CONCAT(kcfTestBody_, __LINE__)()

#define CHECK(expr) do { ++kcftest::Registry::checks(); if (! (expr)) kcftest::reportFailure (__FILE__, __LINE__, #expr); } while (0)
#define CHECK_MSG(expr, msg) do { ++kcftest::Registry::checks(); if (! (expr)) kcftest::reportFailure (__FILE__, __LINE__, std::string (#expr) + " :: " + (msg)); } while (0)
#define CHECK_NEAR(a, b, tol) do { ++kcftest::Registry::checks(); const double _a = (double) (a), _b = (double) (b), _t = (double) (tol); \
    if (! (std::fabs (_a - _b) <= _t)) kcftest::reportFailure (__FILE__, __LINE__, std::string (#a " ~= " #b) + " (" + std::to_string (_a) + " vs " + std::to_string (_b) + ", tol " + std::to_string (_t) + ")"); } while (0)
#define REQUIRE(expr) do { ++kcftest::Registry::checks(); if (! (expr)) { kcftest::reportFailure (__FILE__, __LINE__, std::string ("REQUIRE ") + #expr); return; } } while (0)
