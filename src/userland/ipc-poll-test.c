#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ipc.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("ipc-poll-test FAIL line %d: %s errno=%d\n", __LINE__, #x, errno); ++failures; } } while (0)

static short poll_one(int fd, short events, int timeout) {
  struct pollfd pfd = {.fd = fd, .events = events, .revents = 0};
  int result = poll(&pfd, 1, timeout);
  if (result < 0) return -1;
  return result == 0 ? 0 : pfd.revents;
}

static int g_sender_fd = -1;

static void *delayed_send(void *arg) {
  (void)arg;
  struct timespec delay = {0, 50 * 1000 * 1000};
  nanosleep(&delay, NULL);
  ipc_message_t message = {.length = 1, .attachment_fd = -1};
  message.data[0] = 'w';
  ipc_send(g_sender_fd, &message);
  return NULL;
}

int main(void) {
  int listener = ipc_listen("ipc-poll-test");
  CHECK(listener >= 0);
  if (listener < 0) return 1;
  CHECK(poll_one(listener, POLLIN, 0) == 0);

  int client = ipc_connect("ipc-poll-test");
  CHECK(client >= 0);
  CHECK(poll_one(listener, POLLIN, 0) == POLLIN);
  int server = ipc_accept(listener, 100);
  CHECK(server >= 0);
  CHECK(poll_one(listener, POLLIN, 0) == 0);

  CHECK(poll_one(server, POLLIN | POLLOUT, 0) == POLLOUT);
  CHECK(poll_one(server, POLLIN, 0) == 0);

  ipc_message_t message = {.length = 3, .attachment_fd = -1};
  memcpy(message.data, "abc", 3);
  CHECK(ipc_send(client, &message) == 0);
  CHECK(poll_one(server, POLLIN, 0) == POLLIN);
  ipc_message_t incoming = {.attachment_fd = -1};
  CHECK(ipc_receive(server, &incoming, IPC_NONBLOCK) == 0);
  CHECK(incoming.length == 3 && memcmp(incoming.data, "abc", 3) == 0);
  CHECK(poll_one(server, POLLIN, 0) == 0);

  /* Reveil d'un poll bloque par un envoi depuis un autre thread. */
  g_sender_fd = client;
  pthread_t thread;
  CHECK(pthread_create(&thread, NULL, delayed_send, NULL) == 0);
  CHECK(poll_one(server, POLLIN, 2000) == POLLIN);
  CHECK(pthread_join(thread, NULL) == 0);

  int epfd = epoll_create1(0);
  CHECK(epfd >= 0);
  struct epoll_event event = {.events = EPOLLIN, .data.fd = server};
  CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, server, &event) == 0);
  struct epoll_event out;
  CHECK(epoll_wait(epfd, &out, 1, 0) == 1 && (out.events & EPOLLIN));
  CHECK(ipc_receive(server, &incoming, IPC_NONBLOCK) == 0);
  CHECK(incoming.length == 1 && incoming.data[0] == 'w');
  CHECK(epoll_wait(epfd, &out, 1, 0) == 0);
  close(epfd);

  close(client);
  CHECK((poll_one(server, POLLIN, 0) & POLLHUP) != 0);

  close(server);
  close(listener);
  printf("ipc-poll-test: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}