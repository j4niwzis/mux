// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.workers: work off the UI's thread -- decoding pictures, reading the
// disk -- on a few threads of its own. A job runs on one of them; what it
// made comes back as a callable that the UI's thread runs when the window
// next wakes, for the model and the window are the UI thread's alone.
export module mux.app.workers;

import std;
import mux.app.network;

export namespace mux::app {

class workers {
 public:
  // What a job leaves for the UI's thread to do with what it made.
  using done_t = std::function<void()>;
  using job_t = std::function<done_t()>;

  // One thread fewer than the machine has, one at least, three at most: the
  // UI's keeps a core of its own.
  explicit workers(std::size_t count = default_count()) {
    for (std::size_t i = 0; i < count; ++i)
      threads_.emplace_back([this](std::stop_token stop) { this->work(stop); });
  }
  workers(const workers&) = delete;
  workers& operator=(const workers&) = delete;
  ~workers() {
    for (std::jthread& each : threads_)
      each.request_stop();
    ready_.notify_all();
  }

  // What wakes the window when a job is done: its event's kind, as main
  // registered it -- given once the program is wired.
  void wake_with(std::uint32_t kind) { kind_.store(kind); }

  // A job, to be run on a worker.
  void run(job_t job) {
    {
      std::lock_guard held(lock_);
      jobs_.push_back(std::move(job));
    }
    ready_.notify_one();
  }
  // What the jobs done since left, done now: on the UI's thread.
  void finish() {
    std::vector<done_t> now;
    {
      std::lock_guard held(lock_);
      now = std::exchange(done_, {});
    }
    for (done_t& one : now)
      if (one)
        one();
  }

 private:
  [[nodiscard]] static std::size_t default_count() {
    const unsigned there = std::thread::hardware_concurrency();
    return std::clamp<std::size_t>(there > 1 ? there - 1 : 1, 1, 3);
  }
  void work(std::stop_token stop) {
    while (true) {
      job_t job;
      {
        std::unique_lock held(lock_);
        ready_.wait(held, stop, [&] { return !jobs_.empty(); });
        if (jobs_.empty())
          return;  // asked to stop, and nothing left
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      done_t done = job();
      {
        std::lock_guard held(lock_);
        done_.push_back(std::move(done));
      }
      // The window woken by the kind main registered: a default one is kind
      // 0, no event the window looks at -- what the job made then waited
      // until something else woke it (a picture decoded and not shown
      // until the network said something).
      wake_window{kind_.load()}();
    }
  }

  std::atomic<std::uint32_t> kind_{0};
  std::mutex lock_;
  std::condition_variable_any ready_;
  std::deque<job_t> jobs_;
  std::vector<done_t> done_;
  // Last: stopped and joined before what they use goes.
  std::vector<std::jthread> threads_;
};

}  // namespace mux::app
