// //net sur ALOS : requete HTTP en clair via URLRequestContext (resolveur,
// sockets TCP actifs, pile HTTP/1.1 de Chromium) sur une SingleThreadTaskExecutor
// IO. Cible : un serveur HTTP sur l'hote (10.0.2.2 depuis QEMU user-net).
#include <stdio.h>

#include <memory>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_entry.h"
#include "net/log/net_log_event_type.h"
#include "base/json/json_writer.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"

namespace {

// Journal des evenements reseau (diagnostic) : type, phase et parametres.
class PrintingObserver : public net::NetLog::ThreadSafeObserver {
 public:
  void OnAddEntry(const net::NetLogEntry& entry) override {
    std::string params;
    base::Value::Dict dict = entry.params.Clone();
    if (!dict.empty()) base::JSONWriter::Write(dict, &params);
    printf("net-http-smoke: log %s phase=%d %s\n", net::NetLogEventTypeToString(entry.type),
           static_cast<int>(entry.phase), params.c_str());
  }
};


class Delegate : public net::URLRequest::Delegate {
 public:
  explicit Delegate(base::RepeatingClosure quit)
      : quit_(std::move(quit)), buffer_(base::MakeRefCounted<net::IOBufferWithSize>(4096)) {}

  void OnResponseStarted(net::URLRequest* request, int net_error) override {
    error_ = net_error;
    if (net_error != net::OK) {
      quit_.Run();
      return;
    }
    status_ = request->GetResponseCode();
    Read(request);
  }

  void OnReadCompleted(net::URLRequest* request, int bytes_read) override {
    Consume(request, bytes_read);
  }

  int error() const { return error_; }
  int status() const { return status_; }
  const std::string& body() const { return body_; }

 private:
  void Read(net::URLRequest* request) {
    int rv = request->Read(buffer_.get(), buffer_->size());
    if (rv != net::ERR_IO_PENDING) Consume(request, rv);
  }
  void Consume(net::URLRequest* request, int rv) {
    while (rv > 0) {
      body_.append(buffer_->data(), rv);
      rv = request->Read(buffer_.get(), buffer_->size());
    }
    if (rv == net::ERR_IO_PENDING) return;
    if (rv < 0) error_ = rv;
    quit_.Run();
  }

  base::RepeatingClosure quit_;
  scoped_refptr<net::IOBufferWithSize> buffer_;
  std::string body_;
  int error_ = net::ERR_IO_PENDING;
  int status_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("net-http-smoke");

  const char* url = argc > 1 ? argv[1] : "http://10.0.2.2:8123/hello.txt";
  PrintingObserver observer;
  net::NetLog::Get()->AddObserver(&observer, net::NetLogCaptureMode::kEverything);
  net::URLRequestContextBuilder builder;
  builder.set_proxy_config_service(std::make_unique<net::ProxyConfigServiceFixed>(
      net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context = builder.Build();

  base::RunLoop loop;
  Delegate delegate(loop.QuitClosure());
  std::unique_ptr<net::URLRequest> request = context->CreateRequest(
      GURL(url), net::DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);
  request->Start();
  // Garde-fou : 20 s.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Seconds(20));
  loop.Run();

  request.reset();
  context.reset();
  net::NetLog::Get()->RemoveObserver(&observer);
  printf("net-http-smoke: url=%s error=%d (%s) status=%d body_bytes=%d\n", url,
         delegate.error(), net::ErrorToString(delegate.error()).c_str(),
         delegate.status(), static_cast<int>(delegate.body().size()));
  printf("net-http-smoke: body=[%s]\n", delegate.body().c_str());
  if (delegate.error() == net::OK && delegate.status() == 200 &&
      delegate.body().find("hello from the ALOS host") != std::string::npos) {
    printf("net-http-smoke: PASS\n");
    return 0;
  }
  printf("net-http-smoke: FAIL\n");
  return 1;
}
