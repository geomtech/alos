// base::ThreadPool (TaskScheduler) sur ALOS : taches en parallele sur le pool,
// PostTaskAndReplyWithResult vers la SingleThreadTaskExecutor, SequencedTaskRunner
// (ordre FIFO garanti) et attente via RunLoop.
#include <stdio.h>

#include <atomic>
#include <vector>

#include "base/at_exit.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"

namespace {
int Square(int v) { return v * v; }
}  // namespace

int main() {
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::DEFAULT);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("alos-smoke");

  // 1. PostTaskAndReplyWithResult : calcul sur le pool, reponse sur le thread principal.
  constexpr int kTasks = 16;
  std::atomic<int> replies{0};
  std::atomic<long> sum{0};
  base::RunLoop loop1;
  for (int i = 1; i <= kTasks; i++) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {}, base::BindOnce(&Square, i),
        base::BindOnce(
            [](std::atomic<int>* r, std::atomic<long>* s, base::RepeatingClosure quit, int v) {
              s->fetch_add(v);
              if (r->fetch_add(1) + 1 == kTasks) quit.Run();
            },
            &replies, &sum, loop1.QuitClosure()));
  }
  loop1.Run();

  // 2. SequencedTaskRunner : ordre FIFO strict malgre plusieurs threads.
  auto runner = base::ThreadPool::CreateSequencedTaskRunner({});
  std::vector<int> order;
  base::RunLoop loop2;
  for (int i = 0; i < 20; i++) {
    runner->PostTask(FROM_HERE, base::BindOnce([](std::vector<int>* o, int v) { o->push_back(v); }, &order, i));
  }
  runner->PostTaskAndReply(FROM_HERE, base::DoNothing(), loop2.QuitClosure());
  loop2.Run();
  bool fifo = order.size() == 20;
  for (size_t i = 0; i < order.size(); i++) fifo = fifo && order[i] == static_cast<int>(i);

  base::ThreadPoolInstance::Get()->Shutdown();
  long expected = 0;
  for (int i = 1; i <= kTasks; i++) expected += i * i;
  printf("threadpool-smoke: replies=%d sum=%ld expected=%ld fifo=%d" "\n", replies.load(), sum.load(), expected, fifo);
  if (replies.load() == kTasks && sum.load() == expected && fifo) {
    printf("threadpool-smoke: PASS" "\n");
    return 0;
  }
  printf("threadpool-smoke: FAIL" "\n");
  return 1;
}
