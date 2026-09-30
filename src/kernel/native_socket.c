#include "native_socket.h"
#include "native_poll.h"
#include "native_network_cleanup.h"
#include "process.h"
#include "uaccess.h"
#include "unix_socket.h"
#include "../include/errno.h"
#include "../net/l4/tcp.h"
#include "../net/core/net.h"
#include "../net/core/netdev.h"
#include "../mm/kheap.h"

static spinlock_t resources_lock = {0};
static native_network_resources_t *resources;
void native_network_resources_register(native_network_resources_t *r) {
    r->thread = thread_current();
    uint64_t flags = spinlock_irqsave(&resources_lock);
    r->own_description = r->description != NULL;
    for (native_network_resources_t *p = resources; p; p = p->next)
        if (p->thread == r->thread && p->description == r->description)
            r->own_description = false;
    r->next = resources; resources = r;
    spinlock_irqrestore(&resources_lock,flags);
}
void native_network_resources_unregister(native_network_resources_t *r) {
    uint64_t flags = spinlock_irqsave(&resources_lock);
    native_network_resources_t **p = &resources;
    while (*p && *p != r) p = &(*p)->next;
    if (*p) *p = r->next;
    spinlock_irqrestore(&resources_lock,flags);
}
static void cleanup_resources(thread_t *thread, bool poll_resources) {
    if (!thread) return;
    if (thread->current_wait_queue) {
        wait_queue_remove(thread->current_wait_queue,thread);
        thread->current_wait_queue = NULL;
    }
    for (;;) {
        uint64_t flags = spinlock_irqsave(&resources_lock);
        native_network_resources_t **p = &resources;
        while (*p && ((*p)->thread != thread ||
                      (*p)->poll_resources != poll_resources)) p = &(*p)->next;
        native_network_resources_t *r = *p;
        if (r) *p = r->next;
        spinlock_irqrestore(&resources_lock,flags);
        if (!r) break;
        if (r->own_description) file_description_release(r->description);
        for (uint64_t i = 0; i < r->description_count; ++i)
            file_description_release(r->descriptions[i]);
        kfree(r->allocation1); kfree(r->allocation2);
    }
}
void native_socket_thread_cleanup(thread_t *thread) { cleanup_resources(thread,false); }
void native_poll_thread_cleanup(thread_t *thread) { cleanup_resources(thread,true); }
void native_network_thread_cleanup(thread_t *thread) {
    native_socket_thread_cleanup(thread);
    native_poll_thread_cleanup(thread);
}

static file_descriptor_t *table(void) {
    process_t *p = process_current();
    return p ? p->fd_table : NULL;
}
static bool terminating(void) {
    thread_t *t = thread_current();
    return t && t->should_terminate;
}
static bool readable(void *ctx) {
    tcp_socket_t *s = ctx;
    return s->recv_count || s->state != TCP_STATE_ESTABLISHED ||
           s->read_shutdown || s->socket_error || terminating();
}
static bool accept_ready(void *ctx) {
    tcp_socket_t *s = ctx;
    return tcp_native_ready_client(s->local_port) ||
           s->state != TCP_STATE_LISTEN || terminating();
}
short native_socket_poll(open_file_description_t *d, short events) {
    tcp_socket_t *s = d->socket;
    short r = 0;
    if (s->socket_error) r |= ALOS_POLLERR;
    if (s->state == TCP_STATE_LISTEN) {
        if (tcp_native_ready_client(s->local_port)) r |= events & ALOS_POLLIN;
    } else {
        if (s->recv_count || s->read_shutdown || s->state == TCP_STATE_CLOSE_WAIT ||
            (s->state == TCP_STATE_CLOSED && s->remote_port) ||
            s->socket_error) r |= events & ALOS_POLLIN;
        if ((s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_CLOSE_WAIT) &&
            !s->write_shutdown) r |= events & ALOS_POLLOUT;
        if (s->state == TCP_STATE_CLOSED && s->remote_port) r |= ALOS_POLLHUP;
        if (s->read_shutdown && s->write_shutdown) r |= ALOS_POLLHUP;
    }
    return r;
}
int64_t native_socket_read(open_file_description_t *d, void *buffer,
                           uint64_t count, int flags) {
    if (flags & ~(ALOS_MSG_PEEK | ALOS_MSG_DONTWAIT)) return -EOPNOTSUPP;
    if (count > INT64_MAX) return -EINVAL;
    tcp_socket_t *s = d->socket;
    if (!count) return 0;
    if (!user_range_valid(buffer, count, true)) return -EFAULT;
    uint8_t *storage = kmalloc(TCP_RECV_BUFFER_SIZE);
    if (!storage) return -ENOMEM;
    native_network_resources_t held = {
        .description = d, .allocation1 = storage
    };
    native_network_resources_register(&held);
    int64_t result;
    for (;;) {
        net_lock();
        if (s->recv_count) {
            uint64_t n = count < s->recv_count ? count : s->recv_count;
            for (uint64_t i = 0; i < n; ++i)
                storage[i] = s->recv_buffer[(s->recv_tail + i) % TCP_RECV_BUFFER_SIZE];
            /* UP : copie et consommation atomiques vis-a-vis des IRQ reseau. */
            if (copy_to_user(buffer, storage, n)) result = -EFAULT;
            else {
                if (!(flags & ALOS_MSG_PEEK)) {
                    s->recv_tail = (s->recv_tail + n) % TCP_RECV_BUFFER_SIZE;
                    s->recv_count -= n;
                    s->window = TCP_RECV_BUFFER_SIZE - s->recv_count;
                    tcp_send_packet(s,TCP_FLAG_ACK,NULL,0);
                }
                result = (int64_t)n;
            }
            net_unlock(); break;
        }
        if (s->read_shutdown || s->state == TCP_STATE_CLOSE_WAIT) result = 0;
        else if (s->socket_error) result = -s->socket_error;
        else if (s->state == TCP_STATE_CLOSED && s->remote_port) result = 0;
        else if (s->state != TCP_STATE_ESTABLISHED) result = -ENOTCONN;
        else if ((d->flags & O_NONBLOCK) || (flags & ALOS_MSG_DONTWAIT)) result = -EAGAIN;
        else { net_unlock(); wait_queue_wait(&s->recv_waitqueue, readable, s);
            if (terminating()) { result = -EINTR; break; }
            continue;
        }
        net_unlock(); break;
    }
    native_network_resources_unregister(&held);
    kfree(storage); native_poll_notify(); return result;
}
int64_t native_socket_write(open_file_description_t *d, const void *buffer,
                            uint64_t count, int flags) {
    if (flags & ~(ALOS_MSG_DONTWAIT | ALOS_MSG_NOSIGNAL)) return -EOPNOTSUPP;
    if (count > INT64_MAX) return -EINVAL;
    if (!count) return 0;
    uint64_t n = count > 1460 ? 1460 : count;
    uint8_t *storage = kmalloc(n);
    if (!storage) return -ENOMEM;
    if (copy_from_user(storage, buffer, n)) { kfree(storage); return -EFAULT; }
    net_lock();
    int result = tcp_send_checked(d->socket, storage, (int)n);
    net_unlock(); kfree(storage); return result;
}
static int address_input(sockaddr_in_t *out, const void *p, uint64_t n) {
    if (n < sizeof(*out)) return -EINVAL;
    if (copy_from_user(out, p, sizeof(*out))) return -EFAULT;
    return out->sin_family == AF_INET ? 0 : -EAFNOSUPPORT;
}
static int address_validate(void *address, uint32_t *length, uint32_t *capacity) {
    if (!length || copy_from_user(capacity, length, sizeof(*capacity)) ||
        !user_range_valid(length, sizeof(*length), true)) return -EFAULT;
    size_t n = *capacity < sizeof(sockaddr_in_t) ? *capacity : sizeof(sockaddr_in_t);
    return user_range_valid(address, n, true) ? 0 : -EFAULT;
}
static int address_output(tcp_socket_t *s, bool peer, void *address,
                           uint32_t *length, uint32_t capacity) {
    sockaddr_in_t a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(peer ? s->remote_port : s->local_port);
    if (peer) {
        for (int i = 0; i < 4; ++i) ((uint8_t *)&a.sin_addr)[i] = s->remote_ip[i];
    } else {
        NetInterface *ni = netif_get_default();
        a.sin_addr = s->bound_address;
        if (!a.sin_addr && s->remote_port && ni) a.sin_addr = htonl(ni->ip_addr);
    }
    size_t n = capacity < sizeof(a) ? capacity : sizeof(a);
    uint32_t actual = sizeof(a);
    if (copy_to_user(address, &a, n) ||
        copy_to_user(length, &actual, sizeof(actual))) return -EFAULT;
    return 0;
}
static int create_socket(int family, int type, int protocol) {
    if (family != AF_INET) return -EAFNOSUPPORT;
    int base = type & ~(ALOS_SOCK_NONBLOCK | ALOS_SOCK_CLOEXEC);
    if (base != SOCK_STREAM) return -EPROTONOSUPPORT;
    if (protocol && protocol != IPPROTO_TCP) return -EPROTONOSUPPORT;
    file_descriptor_t *fds = table();
    if (!fds) return -ESRCH;
    net_lock();
    tcp_socket_t *s = tcp_socket_create();
    if (s) { s->native_posix = true; s->window = TCP_RECV_BUFFER_SIZE; }
    net_unlock();
    if (!s) return -ENOBUFS;
    open_file_description_t *d = file_description_create(FILE_TYPE_SOCKET,
        O_RDWR | (type & ALOS_SOCK_NONBLOCK ? O_NONBLOCK : 0), s);
    if (!d) { net_lock(); tcp_close(s); net_unlock(); return -ENOMEM; }
    int fd = file_table_install_flags(fds, d, type & ALOS_SOCK_CLOEXEC ? FD_CLOEXEC : 0);
    if (fd < 0) { file_description_release(d); return -EMFILE; }
    native_poll_notify();
    return fd;
}
static int bind_socket(tcp_socket_t *s, const void *p, uint64_t n) {
    sockaddr_in_t a;
    int error = address_input(&a, p, n);
    if (error) return error;
    NetInterface *ni = netif_get_default();
    if (a.sin_addr && (!ni || a.sin_addr != htonl(ni->ip_addr))) return -EADDRNOTAVAIL;
    net_lock();
    if (s->local_port || s->state != TCP_STATE_CLOSED) error = -EINVAL;
    else if (a.sin_port) error = tcp_bind(s, ntohs(a.sin_port)) ? -EADDRINUSE : 0;
    else {
        error = -EADDRINUSE;
        for (unsigned port = 49152; port <= 65535; ++port)
            if (!tcp_bind(s, (uint16_t)port)) { error = 0; break; }
    }
    if (!error) s->bound_address = a.sin_addr;
    net_unlock(); return error;
}
static int accept_socket(open_file_description_t *d, void *a, uint32_t *len, int flags) {
    if (flags & ~(ALOS_SOCK_NONBLOCK | ALOS_SOCK_CLOEXEC)) return -EINVAL;
    uint32_t capacity = 0;
    if (a) { int error = address_validate(a,len,&capacity); if (error) return error; }
    tcp_socket_t *s = d->socket;
    for (;;) {
        net_lock();
        if (s->state != TCP_STATE_LISTEN) { net_unlock(); return -EINVAL; }
        tcp_socket_t *client = tcp_native_ready_client(s->local_port);
        if (client) {
            client->flags |= TCP_SOCK_ACCEPTED;
            net_unlock();
            open_file_description_t *cd = file_description_create(FILE_TYPE_SOCKET,
                O_RDWR | (flags & ALOS_SOCK_NONBLOCK ? O_NONBLOCK : 0), client);
            if (!cd) {
                net_lock(); client->flags &= ~TCP_SOCK_ACCEPTED; net_unlock();
                return -ENOMEM;
            }
            int fd = file_table_install_flags(table(),cd,
                flags & ALOS_SOCK_CLOEXEC ? FD_CLOEXEC : 0);
            if (fd < 0) { file_description_release(cd); return -EMFILE; }
            int error = a ? address_output(client,true,a,len,capacity) : 0;
            if (error) { file_table_close(table(),fd); return error; }
            native_poll_notify(); return fd;
        }
        net_unlock();
        if (d->flags & O_NONBLOCK) return -EAGAIN;
        wait_queue_wait(&s->accept_waitqueue,accept_ready,s);
        if (terminating()) return -EINTR;
    }
}
int64_t native_socket_call(uint64_t op, uint64_t a, uint64_t b,
                           uint64_t c, uint64_t d, uint64_t e) {
    if (op == ALOS_SOCKET_CREATE) return create_socket((int)a,(int)b,(int)c);
    if (op == ALOS_SOCKET_PAIR) return unix_socket_pair((int)a,(int)b,(int)c,(void *)d);
    file_descriptor_t *fds = table();
    if (!fds) return -ESRCH;
    open_file_description_t *description = file_table_acquire(fds,(int)a);
    if (!description) return -EBADF;
    if (description->type == FILE_TYPE_UNIX_SOCKET) {
        native_network_resources_t held = {.description = description};
        native_network_resources_register(&held);
        int64_t result = unix_socket_call(description,op,b,c,d,e);
        native_network_resources_unregister(&held);
        file_description_release(description);
        return result;
    }
    if (description->type != FILE_TYPE_SOCKET) {
        file_description_release(description); return -ENOTSOCK;
    }
    native_network_resources_t held = {.description = description};
    native_network_resources_register(&held);
    tcp_socket_t *s = description->socket;
    int64_t result = -EOPNOTSUPP;
    uint32_t capacity;
    switch (op) {
    case ALOS_SOCKET_BIND: result = bind_socket(s,(void *)b,c); break;
    case ALOS_SOCKET_LISTEN:
        if ((int)b < 0) result = -EINVAL;
        else if (s->state != TCP_STATE_CLOSED && s->state != TCP_STATE_LISTEN)
            result = -EINVAL;
        else {
            if (!s->local_port) {
                result = -EADDRINUSE;
                net_lock();
                for (unsigned port = 49152; port <= 65535; ++port)
                    if (!tcp_bind(s,(uint16_t)port)) { result = 0; break; }
                net_unlock();
            } else result = 0;
            if (!result) {
                net_lock();
                s->native_backlog = b > 128 ? 128 : b ? (unsigned)b : 1;
                s->state = TCP_STATE_LISTEN;
                net_unlock();
                native_poll_notify();
            }
        }
        break;
    case ALOS_SOCKET_ACCEPT:
        result = accept_socket(description,(void *)b,(uint32_t *)c,(int)d); break;
    case ALOS_SOCKET_SEND:
        result = native_socket_write(description,(void *)b,c,(int)d); break;
    case ALOS_SOCKET_RECV:
        result = native_socket_read(description,(void *)b,c,(int)d); break;
    case ALOS_SOCKET_GETNAME:
    case ALOS_SOCKET_GETPEER:
        if (op == ALOS_SOCKET_GETPEER && !s->remote_port) result = -ENOTCONN;
        else {
            result = address_validate((void *)b,(uint32_t *)c,&capacity);
            if (!result) result = address_output(s,op == ALOS_SOCKET_GETPEER,
                                                 (void *)b,(uint32_t *)c,capacity);
        }
        break;
    case ALOS_SOCKET_GETOPT:
        if ((int)b != ALOS_SOL_SOCKET) result = -ENOPROTOOPT;
        else if (copy_from_user(&capacity,(void *)e,sizeof(capacity))) result = -EFAULT;
        else if (capacity < sizeof(int)) result = -EINVAL;
        else if (!user_range_valid((void *)d,sizeof(int),true) ||
                 !user_range_valid((void *)e,sizeof(uint32_t),true)) result = -EFAULT;
        else {
            int value;
            if ((int)c == ALOS_SO_TYPE) value = SOCK_STREAM;
            else if ((int)c == ALOS_SO_ERROR) value = s->socket_error;
            else if ((int)c == ALOS_SO_ACCEPTCONN) value = s->state == TCP_STATE_LISTEN;
            else { result = -ENOPROTOOPT; break; }
            capacity = sizeof(value);
            result = copy_to_user((void *)d,&value,sizeof(value)) ||
                     copy_to_user((void *)e,&capacity,sizeof(capacity)) ? -EFAULT : 0;
            if (!result && (int)c == ALOS_SO_ERROR) s->socket_error = 0;
        }
        break;
    case ALOS_SOCKET_SETOPT: result = -ENOPROTOOPT; break;
    case ALOS_SOCKET_CONNECT:
        {
            sockaddr_in_t addr;
            result = address_input(&addr,(void *)b,c);
            if (!result) result = -EOPNOTSUPP;
        }
        break;
    case ALOS_SOCKET_SHUTDOWN:
        if ((int)b < 0 || (int)b > 2) result = -EINVAL;
        else if (s->state != TCP_STATE_ESTABLISHED && s->state != TCP_STATE_CLOSE_WAIT)
            result = -ENOTCONN;
        else if ((int)b != 0) result = -EOPNOTSUPP; /* FIN fiable reste a implementer. */
        else {
            net_lock(); s->read_shutdown = true; s->recv_count = 0; net_unlock();
            wait_queue_wake_all(&s->recv_waitqueue); native_poll_notify(); result = 0;
        }
        break;
    default: result = -EINVAL; break;
    }
    native_network_resources_unregister(&held);
    file_description_release(description); return result;
}
