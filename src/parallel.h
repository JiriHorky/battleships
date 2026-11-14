// Minimal parallel loop used by the DP solver.
#pragma once
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace bs {

// Calls fn(i, tid) for every i in [0, n) on `nthreads` threads (the calling
// thread is tid 0); items are handed out dynamically. The first exception
// thrown by any thread stops the loop and is rethrown to the caller.
template <typename F>
inline void parallel_for(int nthreads, size_t n, F fn) {
	std::atomic<size_t> next(0);
	std::exception_ptr err;
	std::mutex err_mu;
	auto work = [&](int tid) {
		try {
			for (size_t i; (i = next.fetch_add(1, std::memory_order_relaxed)) < n;) fn(i, tid);
		} catch (...) {
			next = n;  // stop the other workers
			std::lock_guard<std::mutex> lk(err_mu);
			if (!err) err = std::current_exception();
		}
	};
	if (nthreads <= 1 || n <= 1) {
		work(0);
	} else {
		std::vector<std::thread> th;
		try {
			for (int t = 1; t < nthreads; t++) th.emplace_back(work, t);
		} catch (...) {  // could not start a thread: stop, join the started ones, rethrow
			next = n;
			for (auto & t : th) t.join();
			throw;
		}
		work(0);
		for (auto & t : th) t.join();
	}
	if (err) std::rethrow_exception(err);  // surface worker exceptions to the caller
}

}  // namespace bs
