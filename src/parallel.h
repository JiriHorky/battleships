// Minimal parallel loop used by the DP solver.
#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bs {

namespace detail {

// Persistent worker threads. The solver runs a few short parallel loops per
// board cell (hundreds per solve), so starting threads for each loop would
// dominate small solves. Workers spin briefly for the next loop, then sleep.
class ThreadPool {
public:
	static ThreadPool & get() {
		static ThreadPool * pool = new ThreadPool();  // never destroyed: workers are detached
		return *pool;
	}
	// Serializes users: a caller that finds the pool busy (another solve is
	// running concurrently) gets false and should use its own threads.
	bool try_acquire() { return busy_.try_lock(); }
	void release() { busy_.unlock(); }

	// Runs work(tid) for tid in [0, n), tid 0 on the calling thread. Must hold
	// the pool (try_acquire).
	void run(int n, const std::function<void(int)> & work) {
		while ((int)nworkers_ < n - 1) {
			std::thread(&ThreadPool::worker, this, (int)nworkers_ + 1, gen_.load()).detach();
			nworkers_++;
		}
		{
			std::lock_guard<std::mutex> lk(mu_);
			job_ = &work;
			want_ = n;
			remaining_.store(n - 1);
			gen_.fetch_add(1, std::memory_order_release);
		}
		cv_.notify_all();
		work(0);
		if (remaining_.load(std::memory_order_acquire)) {
			for (int spin = 0; spin < 20000 && remaining_.load(std::memory_order_acquire); spin++) cpu_relax();
			std::unique_lock<std::mutex> lk(mu_);
			done_cv_.wait(lk, [&] { return remaining_.load(std::memory_order_acquire) == 0; });
		}
	}

private:
	static inline void cpu_relax() {
#if defined(__aarch64__)
		asm volatile("yield");
#elif defined(__x86_64__)
		__builtin_ia32_pause();
#endif
	}
	// `seen`: the last loop generation before this worker was started
	void worker(int tid, uint64_t seen) {
		for (;;) {
			// wait for a new loop: spin for a while, then sleep
			for (int spin = 0; spin < 20000 && gen_.load(std::memory_order_acquire) == seen; spin++) cpu_relax();
			if (gen_.load(std::memory_order_acquire) == seen) {
				std::unique_lock<std::mutex> lk(mu_);
				cv_.wait(lk, [&] { return gen_.load(std::memory_order_acquire) != seen; });
			}
			const std::function<void(int)> * job;
			int want;
			{
				std::lock_guard<std::mutex> lk(mu_);
				seen = gen_.load(std::memory_order_acquire);
				job = job_;
				want = want_;
			}
			if (tid >= want) continue;  // this loop uses fewer threads
			(*job)(tid);
			if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				std::lock_guard<std::mutex> lk(mu_);
				done_cv_.notify_one();
			}
		}
	}

	std::mutex busy_;
	std::mutex mu_;
	std::condition_variable cv_, done_cv_;
	std::atomic<uint64_t> gen_{0};
	std::atomic<int> remaining_{0};
	const std::function<void(int)> * job_ = nullptr;
	int want_ = 0;
	size_t nworkers_ = 0;
};

}  // namespace detail

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
	} else if (detail::ThreadPool::get().try_acquire()) {
		struct Release {
			~Release() { detail::ThreadPool::get().release(); }
		} release;
		detail::ThreadPool::get().run(nthreads, work);
	} else {  // pool in use by a concurrent solve: start threads for this loop
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
