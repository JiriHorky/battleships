// Minimal test harness (no external dependencies).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
	const char * name;
	std::function<void()> fn;
	bool slow;
};
std::vector<TestCase> & test_registry();
extern int g_failures;

struct TestRegistrar {
	TestRegistrar(const char * n, std::function<void()> f, bool slow) { test_registry().push_back({n, f, slow}); }
};

#define TEST_IMPL(name, slow)                                       \
	static void name();                                             \
	static TestRegistrar reg_##name(#name, name, slow);             \
	static void name()
#define TEST(name) TEST_IMPL(name, false)
#define SLOW_TEST(name) TEST_IMPL(name, true)

#define CHECK(cond)                                                                        \
	do {                                                                                   \
		if (!(cond)) {                                                                     \
			fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
			g_failures++;                                                                  \
		}                                                                                  \
	} while (0)

#define CHECK_EQ(a, b)                                                                     \
	do {                                                                                   \
		auto _a = (a);                                                                     \
		auto _b = (b);                                                                     \
		if (!(_a == _b)) {                                                                 \
			fprintf(stderr, "  FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, \
			        #b, bs::to_string(_a).c_str(), bs::to_string(_b).c_str());             \
			g_failures++;                                                                  \
		}                                                                                  \
	} while (0)
