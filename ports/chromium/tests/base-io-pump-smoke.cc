// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/task/current_thread.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/threading/thread.h"
#include "base/time/time.h"

namespace {
using Pump = base::MessagePumpForIO;
using Runner = base::SingleThreadTaskRunner;

struct Pipe {
  Pipe() {
    int fds[2];
    PCHECK(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
    reader.reset(fds[0]);
    writer.reset(fds[1]);
  }
  base::ScopedFD reader, writer;
};

class Watcher : public Pump::FdWatcher {
 public:
  base::RepeatingCallback<void(int)> on_read;
  base::RepeatingCallback<void(int)> on_write;
  Pump::FdWatchController controller{FROM_HERE};

  void Watch(int fd, bool persistent, int mode) {
    CHECK(base::CurrentIOThread::Get()->WatchFileDescriptor(
        fd, persistent, static_cast<Pump::Mode>(mode), &controller, this));
  }
  void OnFileCanReadWithoutBlocking(int fd) override {
    auto callback = on_read;
    CHECK(callback);
    callback.Run(fd);
  }
  void OnFileCanWriteWithoutBlocking(int fd) override {
    auto callback = on_write;
    CHECK(callback);
    callback.Run(fd);
  }
};

std::shared_ptr<bool> Deadline() {
  auto complete = std::make_shared<bool>(false);
  Runner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce([](std::shared_ptr<bool> done) {
        CHECK(*done) << "ALOS IO-pump smoke timed out";
      }, complete),
      base::Seconds(10));
  return complete;
}
void PostQuit(base::RunLoop& loop) {
  Runner::GetCurrentDefault()->PostTask(FROM_HERE, loop.QuitClosure());
}
void WriteByte(int fd) {
  const char byte = 'x';
  PCHECK(write(fd, &byte, 1) == 1);
}
void ReadByte(int fd) {
  char byte;
  PCHECK(read(fd, &byte, 1) == 1);
}

void PersistentAndRearm() {
  for (bool persistent : {false, true}) {
    Pipe pipe;
    Watcher watcher;
    base::RunLoop loop;
    auto complete = Deadline();
    int calls = 0;
    watcher.on_read = base::BindLambdaForTesting([&](int fd) {
      ReadByte(fd);
      ++calls;
      if (calls == 1) {
        if (!persistent) {
          watcher.Watch(fd, false, Pump::WATCH_READ);
        }
        WriteByte(pipe.writer.get());
      } else {
        CHECK_EQ(calls, 2);
        CHECK(watcher.controller.StopWatchingFileDescriptor());
        PostQuit(loop);
      }
    });
    watcher.Watch(pipe.reader.get(), persistent, Pump::WATCH_READ);
    WriteByte(pipe.writer.get());
    loop.Run();
    *complete = true;
    CHECK_EQ(calls, 2);
    CHECK(watcher.controller.StopWatchingFileDescriptor());
  }
}

void OneShotStaysDisarmed() {
  Pipe pipe;
  Watcher watcher;
  base::RunLoop loop;
  auto complete = Deadline();
  int calls = 0;
  watcher.on_read = base::BindLambdaForTesting([&](int fd) {
    ReadByte(fd);
    ++calls;
    // Leave another byte readable for subsequent iterations without rearming.
    WriteByte(pipe.writer.get());
    Runner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(20));
  });
  watcher.Watch(pipe.reader.get(), false, Pump::WATCH_READ);
  WriteByte(pipe.writer.get());
  loop.Run();
  *complete = true;
  CHECK_EQ(calls, 1);
  CHECK(watcher.controller.StopWatchingFileDescriptor());
  ReadByte(pipe.reader.get());
}

void CancelAnotherReadyWatch() {
  Pipe pipe;
  Watcher first;
  auto second = std::make_unique<Watcher>();
  base::RunLoop loop;
  auto complete = Deadline();
  int first_calls = 0, second_calls = 0;
  first.on_read = base::BindLambdaForTesting([&](int fd) {
    ReadByte(fd);
    ++first_calls;
    second.reset();
    PostQuit(loop);
  });
  second->on_read = base::BindLambdaForTesting([&](int) { ++second_calls; });
  first.Watch(pipe.reader.get(), false, Pump::WATCH_READ);
  second->Watch(pipe.reader.get(), false, Pump::WATCH_READ);
  WriteByte(pipe.writer.get());
  loop.Run();
  *complete = true;
  CHECK_EQ(first_calls, 1);
  CHECK_EQ(second_calls, 0);
}

void ReadWriteModesAndSelfDeletion() {
  const char* path = "/base-io-pump-smoke.dat";
  base::ScopedFD file(open(path, O_RDWR | O_CREAT | O_EXCL, 0600));
  PCHECK(file.is_valid()) << "Smoke uses a new file on the isolated test disk";
  {
    Watcher watcher;
    base::RunLoop loop;
    auto complete = Deadline();
    int reads = 0, writes = 0;
    watcher.on_write = base::BindLambdaForTesting([&](int) { ++writes; });
    watcher.on_read = base::BindLambdaForTesting([&](int fd) {
      char byte;
      PCHECK(read(fd, &byte, 1) == 0);  // Real regular-file EOF readiness.
      ++reads;
      CHECK(watcher.controller.StopWatchingFileDescriptor());
      PostQuit(loop);
    });
    watcher.Watch(file.get(), true, Pump::WATCH_WRITE);
    watcher.Watch(file.get(), true, Pump::WATCH_READ);
    loop.Run();
    *complete = true;
    CHECK_EQ(reads, 1);
    CHECK_EQ(writes, 1);
  }
  {
    auto watcher = std::make_unique<Watcher>();
    base::RunLoop loop;
    auto complete = Deadline();
    int reads = 0, writes = 0;
    watcher->on_read = base::BindLambdaForTesting([&](int) { ++reads; });
    watcher->on_write = base::BindLambdaForTesting([&](int) {
      ++writes;
      watcher.reset();  // Both the controller and callback owner die here.
      PostQuit(loop);
    });
    watcher->Watch(file.get(), true, Pump::WATCH_READ_WRITE);
    loop.Run();
    *complete = true;
    CHECK_EQ(writes, 1);
    CHECK_EQ(reads, 0);
  }
  file.reset();
  PCHECK(unlink(path) == 0);
}

void HangupAndInvalidDescriptor() {
  {
    Pipe pipe;
    Watcher watcher;
    base::RunLoop loop;
    auto complete = Deadline();
    int calls = 0;
    watcher.on_read = base::BindLambdaForTesting([&](int fd) {
      char byte;
      PCHECK(read(fd, &byte, 1) == 0);
      ++calls;
      CHECK(watcher.controller.StopWatchingFileDescriptor());
      PostQuit(loop);
    });
    watcher.Watch(pipe.reader.get(), true, Pump::WATCH_READ);
    pipe.writer.reset();
    loop.Run();
    *complete = true;
    CHECK_EQ(calls, 1);
  }
  {
    Pipe pipe;
    Watcher watcher;
    int calls = 0;
    watcher.on_read = base::BindLambdaForTesting([&](int) { ++calls; });
    const int fd = pipe.reader.get();
    pipe.reader.reset();
    CHECK(!base::CurrentIOThread::Get()->WatchFileDescriptor(
        fd, true, Pump::WATCH_READ, &watcher.controller, &watcher));
    CHECK(watcher.controller.StopWatchingFileDescriptor());
    CHECK_EQ(calls, 0);
  }
  {
    Pipe pipe;
    Watcher watcher;
    base::RunLoop loop;
    auto complete = Deadline();
    int calls = 0;
    watcher.on_read = base::BindLambdaForTesting([&](int) { ++calls; });
    watcher.Watch(pipe.reader.get(), true, Pump::WATCH_READ);
    // Deliberate contract violation tests diagnosis, not descriptor-reuse safety.
    pipe.reader.reset();
    Runner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(10));
    loop.Run();
    *complete = true;
    CHECK(watcher.controller.StopWatchingFileDescriptor());
    CHECK_EQ(calls, 0);
  }
}

void NestedQuitAndDelayedTask() {
  base::RunLoop outer;
  auto complete = Deadline();
  bool nested_returned = false, delayed_ran = false;
  Runner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindLambdaForTesting([&] {
    base::RunLoop inner(base::RunLoop::Type::kNestableTasksAllowed);
    PostQuit(inner);
    inner.Run();
    nested_returned = true;
    CHECK(base::RunLoop::IsRunningOnCurrentThread());
    const base::TimeTicks start = base::TimeTicks::Now();
    Runner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindLambdaForTesting([&, start] {
          CHECK_GE(base::TimeTicks::Now() - start, base::Milliseconds(20));
          delayed_ran = true;
          outer.Quit();
        }), base::Milliseconds(20));
  }));
  outer.Run();
  *complete = true;
  CHECK(nested_returned);
  CHECK(delayed_ran);
}

void CrossThreadWakeup() {
  base::Thread poster("alos-io-wakeup");
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  CHECK(poster.StartWithOptions(std::move(options)));
  base::RunLoop loop;
  auto complete = Deadline();
  int calls = 0;
  auto target = Runner::GetCurrentDefault();
  auto callback = base::BindLambdaForTesting([&] {
    ++calls;
    if (calls == 512) {
      loop.Quit();
    }
  });
  poster.task_runner()->PostTask(FROM_HERE, base::BindLambdaForTesting(
      [target, callback] {
        for (int i = 0; i < 512; ++i) {
          target->PostTask(FROM_HERE, callback);
        }
      }));
  loop.Run();
  *complete = true;
  CHECK_EQ(calls, 512);
  poster.Stop();  // Exercises wakeup and Quit on the second real IO pump too.
}

void RepeatedSleepWakeup() {
  base::Thread poster("alos-io-sleep-race");
  CHECK(poster.Start());
  base::RunLoop loop;
  auto complete = Deadline();
  auto target = Runner::GetCurrentDefault();
  int replies = 0;
  base::RepeatingClosure exchange;
  exchange = base::BindLambdaForTesting([&] {
    if (++replies == 64) {
      loop.Quit();
      return;
    }
    // The delayed cross-thread reply makes the IO thread enter its blocking
    // poll repeatedly instead of testing only a single burst of queued work.
    poster.task_runner()->PostDelayedTask(
        FROM_HERE, base::BindLambdaForTesting([target, &exchange] {
          target->PostTask(FROM_HERE, exchange);
        }), base::Milliseconds(2));
  });
  poster.task_runner()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([target, &exchange] {
        target->PostTask(FROM_HERE, exchange);
      }));
  loop.Run();
  *complete = true;
  CHECK_EQ(replies, 64);
  poster.Stop();
}

void SaturatedWakePipe() {
  Pump pump;
  // The native pipe capacity is bounded. Excess one-byte wakes must return
  // EAGAIN without treating an already-readable wake pipe as a lost wake.
  for (int i = 0; i < 8192; ++i) {
    pump.ScheduleWork();
  }
}
}  // namespace

int main() {
  base::AtExitManager at_exit;
  SaturatedWakePipe();
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  CHECK(base::CurrentIOThread::IsSet());
  PersistentAndRearm();
  OneShotStaysDisarmed();
  CancelAnotherReadyWatch();
  ReadWriteModesAndSelfDeletion();
  HangupAndInvalidDescriptor();
  NestedQuitAndDelayedTask();
  CrossThreadWakeup();
  RepeatedSleepWakeup();
  puts("base-io-pump-smoke: PASS");
  return 0;
}
