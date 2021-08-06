class Server {
  void WriteToDatabse() {
    // Set up timeout.
    return Timer(loop, 15000 /* milliseconds */)
        | ...;
  }
};


TEST(Libuv, Test) {
  Server server;
  server.WriteToDatabse()
      | ...;


  // ADVANCE TIME!

  Loop::Default().Advance(15s);

  // failure only after 15 seconds!
}


TEST(Libuv, Test) {
  Loop loop;

  Timer()
      | Lambda([]() {

        });


  DomainNameResolve("foo.bar.com")
      | Lambda([](auto&& ip) {
          if (ip.()) {
          }
        });


  int fd;

  auto error = MakeFileDescriptorNonBlocking(fd);

  CHECK(!error);


  fs::Read(fd, min, max)
      | Lambda([](auto&& data) {

        });


  char* buffer = CreateBuffer();
  fs::Read(fd, buffer, size)
      | Lambda([buffer](size_t bytes_read) {
          // use buffer
        });


  fs::Write(fd...);
}


TEST(LibuvTest, Watch) {
  auto e = Repeat()
      | fs::Watch("path/to/file")
      | Lambda([promise]() {
             // file has been updated on filesystem
             promise.set_value(true);
           })
      | Loop();


  auto future, k = Terminate(e);

  start(k);

  write(to local file);


  EXPECT_TRUE(future.get());
}


struct Clock {
  bool Pause() {
    paused_ = ev_now();
  }

  void Advance(uint64_t milliseconds) {
    advanced_ += milliseconds;

    // go through and see if any timers have fired after paused_ + advanced_;
    // and do uv_timer_start() on those timers without timeout of 0.
  }

  void Resume() {
    // do uv_timer_start() for remaining 'timers_' with timeout of 'advanced_ - timeout_'
  }


  bool Paused();

  std::deque<std::pair<uv_timer_t*, uint64_t /* timeout */>> timers_;
  uint64_t paused_;
  uint64_t advanced_;
};


Eventual()
    .start([]() {
      if (LIKELY(!Clock::Default().Paused())) {
        ev_timer_start();
      } else {
        // put timer in 'clock list of timers'
      }
    });


TEST() {
  Timer(loop, 4000) // instead of calling 'uv_now()', it will
      | Then([]() {});

  // Timer(1s); // checks that the clock is not paused and does uv_timer_start

  Clock::Default().Pause(); // fails if there are any outstanding timers

  Timer(1s); // sees that clock is paused and adds itself to pending timers at 'paused' time + 'timeout' (1s)

  Clock::Default().Advance(1s); // adds value to 'advanced'and  grabs all timers that are within 1 second of 'paused' + 'advanced' time and does uv_timer_start with timeout of 0

  Clock::Default().Resume(); // grabs all timers that have not fired and does uv_timer_start with timeout of 'paused' + 'advanced' - 'timeout'
}
