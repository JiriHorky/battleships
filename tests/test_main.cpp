#include <chrono>
#include <cstring>

#include "test_util.h"

std::vector<TestCase> & test_registry() {
	static std::vector<TestCase> r;
	return r;
}
int g_failures = 0;

// usage: tests [--slow] [name-substring]
int main(int argc, char ** argv) {
	bool slow = false;
	const char * filter = nullptr;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--slow")) slow = true;
		else filter = argv[i];
	}
	int run = 0, skipped = 0;
	for (auto & t : test_registry()) {
		if (filter && !strstr(t.name, filter)) continue;
		if (t.slow && !slow) {
			skipped++;
			continue;
		}
		int before = g_failures;
		auto t0 = std::chrono::steady_clock::now();
		t.fn();
		double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		printf("%s %-45s %.2fs\n", g_failures == before ? "ok  " : "FAIL", t.name, s);
		run++;
	}
	printf("\n%d tests run, %d skipped (use --slow), %d failed checks\n", run, skipped, g_failures);
	return g_failures ? 1 : 0;
}
