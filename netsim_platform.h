/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_PLATFORM_H
#define NETSIM_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__EMSCRIPTEN__) && (defined(UNIX) || defined(_WIN32))
#define NETSIM_PLATFORM_SUPPORTED 1
#else
#define NETSIM_PLATFORM_SUPPORTED 0
#endif

#if NETSIM_PLATFORM_SUPPORTED

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
typedef struct netsim_mutex {
  CRITICAL_SECTION cs;
} netsim_mutex_t;
typedef struct netsim_cond {
  CONDITION_VARIABLE cv;
} netsim_cond_t;
typedef HANDLE netsim_thread_t;
#else
#include <arpa/inet.h>
#include <pthread.h>
typedef struct netsim_mutex {
  pthread_mutex_t mutex;
} netsim_mutex_t;
typedef struct netsim_cond {
  pthread_cond_t cond;
} netsim_cond_t;
typedef pthread_t netsim_thread_t;
#endif

typedef intptr_t netsim_socket_t;
typedef uint64_t netsim_deadline_t;
typedef void *(*netsim_thread_fn)(void *);

#define NETSIM_SOCKET_INVALID ((netsim_socket_t)-1)

typedef struct netsim_sockaddr {
  struct sockaddr_storage ss;
  socklen_t len;
} netsim_sockaddr_t;

bool netsim_platform_init(void);
void netsim_platform_cleanup(void);

void netsim_mutex_init(netsim_mutex_t *mp);
void netsim_mutex_destroy(netsim_mutex_t *mp);
void netsim_mutex_lock(netsim_mutex_t *mp);
void netsim_mutex_unlock(netsim_mutex_t *mp);

void netsim_cond_init(netsim_cond_t *cp);
void netsim_cond_destroy(netsim_cond_t *cp);
void netsim_cond_wait(netsim_cond_t *cp, netsim_mutex_t *mp);
bool netsim_cond_timedwait(netsim_cond_t *cp, netsim_mutex_t *mp,
  netsim_deadline_t deadline);
void netsim_cond_broadcast(netsim_cond_t *cp);

bool netsim_thread_create(netsim_thread_t *thrp, netsim_thread_fn fn, void *arg);
void netsim_thread_join(netsim_thread_t thr);

netsim_deadline_t netsim_deadline_after_ms(int ms);
bool netsim_deadline_due(netsim_deadline_t deadline);
uint64_t netsim_monotonic_ns(void);
uint32_t netsim_process_id(void);
void netsim_sleep_ms(int ms);

bool netsim_socket_open_udp(const char *local_host, const char *local_port,
  const char *remote_host, const char *remote_port, netsim_socket_t *sockp,
  char *local_desc, size_t local_desc_len, char *peer_desc,
  size_t peer_desc_len, char *errbuf, size_t errbuf_len);
bool netsim_socket_open_bound_udp(const char *local_host, const char *local_port,
  netsim_socket_t *sockp, char *local_desc, size_t local_desc_len,
  char *errbuf, size_t errbuf_len);
void netsim_socket_close(netsim_socket_t sock);
int netsim_socket_send(netsim_socket_t sock, const void *buf, size_t len);
int netsim_socket_recv(netsim_socket_t sock, void *buf, size_t len);
int netsim_socket_sendto(netsim_socket_t sock, const void *buf, size_t len,
  const netsim_sockaddr_t *addrp);
int netsim_socket_recvfrom(netsim_socket_t sock, void *buf, size_t len,
  netsim_sockaddr_t *addrp);
bool netsim_socket_getsockname(netsim_socket_t sock, netsim_sockaddr_t *addrp);
bool netsim_sockaddr_resolve_udp(const char *host, const char *port,
  netsim_sockaddr_t *addrp, char *errbuf, size_t errbuf_len);
bool netsim_sockaddr_local_for_peer(const char *peer_host, const char *peer_port,
  char *hostbuf, size_t hostbuf_len, char *errbuf, size_t errbuf_len);
bool netsim_sockaddr_format(const netsim_sockaddr_t *addrp, char *buf,
  size_t buflen);
bool netsim_sockaddr_same(const netsim_sockaddr_t *ap,
  const netsim_sockaddr_t *bp);
int netsim_socket_last_error(void);
bool netsim_socket_err_wouldblock(int err);
bool netsim_socket_err_transient(int err);
const char *netsim_socket_strerror(int err, char *buf, size_t buflen);

#else

/*
 * Keep public SIP stub prototypes well-typed even on unsupported platforms.
 * No platform helpers are exposed in this configuration.
 */
typedef intptr_t netsim_socket_t;
typedef uint64_t netsim_deadline_t;
typedef struct netsim_sockaddr {
  unsigned char _opaque;
} netsim_sockaddr_t;

#endif

#endif
