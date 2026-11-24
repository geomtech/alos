// V8 embarque dans une boucle d'evenements Chromium (base::RunLoop) sur ALOS.
// Un setTimeout natif (FunctionTemplate) poste des taches retardees sur la
// SingleThreadTaskExecutor ; le RunLoop les execute dans l'ordre des delais et
// rend la main quand la derniere a tourne. Sans gin : V8 + libplatform + base.
#include <stdio.h>

#include <memory>
#include <string>

#include "base/at_exit.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "include/libplatform/libplatform.h"
#include "include/v8-context.h"
#include "include/v8-initialization.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-script.h"
#include "include/v8-template.h"
#include "include/v8-function.h"

namespace {

v8::Isolate* g_isolate;
v8::Global<v8::Context>* g_context;
base::RunLoop* g_loop;
int g_pending;

void Fire(v8::Global<v8::Function>* holder) {
  v8::Isolate::Scope isolate_scope(g_isolate);
  v8::HandleScope handle_scope(g_isolate);
  v8::Local<v8::Context> context = g_context->Get(g_isolate);
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> fn = holder->Get(g_isolate);
  (void)fn->Call(context, context->Global(), 0, nullptr);
  delete holder;
  if (--g_pending == 0) g_loop->Quit();
}

void SetTimeout(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  if (args.Length() < 2 || !args[0]->IsFunction()) return;
  int32_t delay_ms = args[1]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0);
  auto* holder = new v8::Global<v8::Function>(isolate, args[0].As<v8::Function>());
  g_pending++;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&Fire, holder), base::Milliseconds(delay_ms));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::DEFAULT);

  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();

  v8::Isolate::CreateParams params;
  params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  g_isolate = v8::Isolate::New(params);
  int rc = 1;
  {
    v8::Isolate::Scope isolate_scope(g_isolate);
    v8::HandleScope handle_scope(g_isolate);
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(g_isolate);
    global->Set(g_isolate, "setTimeout", v8::FunctionTemplate::New(g_isolate, SetTimeout));
    v8::Local<v8::Context> context = v8::Context::New(g_isolate, nullptr, global);
    g_context = new v8::Global<v8::Context>(g_isolate, context);
    {
      v8::Context::Scope context_scope(context);
      const char* js =
          "var log = [];"
          "setTimeout(function() { log.push('c'); }, 60);"
          "setTimeout(function() { log.push('a'); }, 20);"
          "setTimeout(function() { log.push('b'); setTimeout(function() { log.push('d'); }, 20); }, 40);";
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8(g_isolate, js).ToLocalChecked();
      v8::Script::Compile(context, source).ToLocalChecked()->Run(context).ToLocalChecked();
    }
    base::TimeTicks start = base::TimeTicks::Now();
    base::RunLoop run_loop;
    g_loop = &run_loop;
    run_loop.Run();
    int64_t elapsed_ms = (base::TimeTicks::Now() - start).InMilliseconds();
    {
      v8::Context::Scope context_scope(context);
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8(g_isolate, "log.join(',')").ToLocalChecked();
      v8::Local<v8::Value> result =
          v8::Script::Compile(context, source).ToLocalChecked()->Run(context).ToLocalChecked();
      v8::String::Utf8Value utf8(g_isolate, result);
      printf("v8-runloop-smoke: order=%s elapsed_ms=%ld\n", *utf8, static_cast<long>(elapsed_ms));
      // Delais 20/40/60 ms et 40+20 : a,b,c,d (c et d a 60 ms, c poste avant d).
      bool ok = std::string(*utf8) == "a,b,c,d" && elapsed_ms >= 55 && elapsed_ms < 2000;
      if (ok) {
        printf("v8-runloop-smoke: PASS\n");
        rc = 0;
      } else {
        printf("v8-runloop-smoke: FAIL\n");
      }
    }
    g_context->Reset();
    delete g_context;
  }
  g_isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete params.array_buffer_allocator;
  return rc;
}
