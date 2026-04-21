/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "def.h"
#include "netsim_platform.h"

#if NETSIM_PLATFORM_SUPPORTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <mmsystem.h>
#include <process.h>
#include <ws2tcpip.h>

struct netsim_thread_start {
  netsim_thread_fn fn;
  void *arg;
};

static bool g_wsa_ready = false;
static bool g_timer_period_set = false;

static uint64_t
netsim_now_ms(void)
{
  static LARGE_INTEGER freq = {0};
  LARGE_INTEGER counter;
  uint64_t ticks;
  uint64_t hz;

  if (freq.QuadPart == 0)
    QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  ticks = (uint64_t)counter.QuadPart;
  hz = (uint64_t)freq.QuadPart;
  return ((ticks / hz) * 1000ULL + ((ticks % hz) * 1000ULL) / hz);
}

bool
netsim_platform_init(void)
{
  WSADATA wsa;

  if (g_wsa_ready)
    return (true);
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    return (false);
  if (timeBeginPeriod(1) == TIMERR_NOERROR)
    g_timer_period_set = true;
  g_wsa_ready = true;
  return (true);
}

void
netsim_platform_cleanup(void)
{

  if (!g_wsa_ready)
    return;
  if (g_timer_period_set) {
    timeEndPeriod(1);
    g_timer_period_set = false;
  }
  WSACleanup();
  g_wsa_ready = false;
}

void
netsim_mutex_init(netsim_mutex_t *mp)
{

  InitializeCriticalSection(&mp->cs);
}

void
netsim_mutex_destroy(netsim_mutex_t *mp)
{

  DeleteCriticalSection(&mp->cs);
}

void
netsim_mutex_lock(netsim_mutex_t *mp)
{

  EnterCriticalSection(&mp->cs);
}

void
netsim_mutex_unlock(netsim_mutex_t *mp)
{

  LeaveCriticalSection(&mp->cs);
}

void
netsim_cond_init(netsim_cond_t *cp)
{

  InitializeConditionVariable(&cp->cv);
}

void
netsim_cond_destroy(netsim_cond_t *cp)
{

  (void)cp;
}

void
netsim_cond_wait(netsim_cond_t *cp, netsim_mutex_t *mp)
{

  SleepConditionVariableCS(&cp->cv, &mp->cs, INFINITE);
}

bool
netsim_cond_timedwait(netsim_cond_t *cp, netsim_mutex_t *mp,
  netsim_deadline_t deadline)
{
  uint64_t now;
  DWORD timeout;

  for (;;) {
    now = netsim_now_ms();
    if (deadline <= now)
      return (false);
    timeout = (DWORD)(deadline - now);
    if (timeout == 0)
      timeout = 1;
    if (SleepConditionVariableCS(&cp->cv, &mp->cs, timeout))
      return (true);
    if (GetLastError() != ERROR_TIMEOUT)
      return (true);
  }
}

void
netsim_cond_broadcast(netsim_cond_t *cp)
{

  WakeAllConditionVariable(&cp->cv);
}

static unsigned __stdcall
netsim_thread_trampoline(void *arg)
{
  struct netsim_thread_start *tsp;

  tsp = (struct netsim_thread_start *)arg;
  tsp->fn(tsp->arg);
  free(tsp);
  return (0);
}

bool
netsim_thread_create(netsim_thread_t *thrp, netsim_thread_fn fn, void *arg)
{
  struct netsim_thread_start *tsp;
  uintptr_t thr;

  tsp = calloc(1, sizeof(*tsp));
  if (tsp == NULL)
    return (false);
  tsp->fn = fn;
  tsp->arg = arg;
  thr = _beginthreadex(NULL, 0, netsim_thread_trampoline, tsp, 0, NULL);
  if (thr == 0) {
    free(tsp);
    return (false);
  }
  *thrp = (HANDLE)thr;
  return (true);
}

void
netsim_thread_join(netsim_thread_t thr)
{

  WaitForSingleObject(thr, INFINITE);
  CloseHandle(thr);
}

netsim_deadline_t
netsim_deadline_after_ms(int ms)
{

  return (netsim_now_ms() + (uint64_t)ms);
}

bool
netsim_deadline_due(netsim_deadline_t deadline)
{

  return (netsim_now_ms() >= deadline);
}

uint64_t
netsim_monotonic_ns(void)
{
  static LARGE_INTEGER freq = {0};
  LARGE_INTEGER counter;
  uint64_t ticks;
  uint64_t hz;

  if (freq.QuadPart == 0)
    QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  ticks = (uint64_t)counter.QuadPart;
  hz = (uint64_t)freq.QuadPart;
  return ((ticks / hz) * 1000000000ULL +
    ((ticks % hz) * 1000000000ULL) / hz);
}

uint32_t
netsim_process_id(void)
{

  return ((uint32_t)GetCurrentProcessId());
}

void
netsim_sleep_ms(int ms)
{

  Sleep((DWORD)ms);
}

static void
format_sockaddr(const struct sockaddr_in *sap, char *buf, size_t buflen)
{
  char ipbuf[INET_ADDRSTRLEN];

  if (inet_ntop(AF_INET, &sap->sin_addr, ipbuf, sizeof(ipbuf)) == NULL) {
    strncpy(buf, "<unknown>", buflen);
    buf[buflen - 1] = '\0';
    return;
  }
  snprintf(buf, buflen, "%s:%u", ipbuf, (unsigned int)ntohs(sap->sin_port));
}

static bool
resolve_udp_addr(const char *host, const char *port, bool passive,
  struct sockaddr_in *sap, char *errbuf, size_t errbuf_len)
{
  struct addrinfo hints, *res;
  const char *rhost;
  int gres;

  memset(&hints, '\0', sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  if (passive)
    hints.ai_flags = AI_PASSIVE;
  rhost = (host != NULL && host[0] != '\0') ? host : NULL;
  gres = getaddrinfo(rhost, port, &hints, &res);
  if (gres != 0) {
    snprintf(errbuf, errbuf_len, "cannot resolve %s%s%s: %s",
      rhost != NULL ? rhost : "*", port != NULL ? ":" : "",
      port != NULL ? port : "", gai_strerrorA(gres));
    return (false);
  }
  memcpy(sap, res->ai_addr, sizeof(*sap));
  freeaddrinfo(res);
  return (true);
}

static bool
set_nonblocking_socket(SOCKET sock, char *errbuf, size_t errbuf_len)
{
  u_long one;

  one = 1;
  if (ioctlsocket(sock, FIONBIO, &one) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  return (true);
}

static bool
set_socket_recv_timeout(SOCKET sock, int timeout_ms, char *errbuf,
  size_t errbuf_len)
{
  DWORD timeout;

  timeout = (DWORD)timeout_ms;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
        sizeof(timeout)) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  return (true);
}

bool
netsim_socket_open_udp(const char *local_host, const char *local_port,
  const char *remote_host, const char *remote_port, netsim_socket_t *sockp,
  char *local_desc, size_t local_desc_len, char *peer_desc,
  size_t peer_desc_len, char *errbuf, size_t errbuf_len)
{
  struct addrinfo hints, local_hints, *res, *rp, *lres, *lrp;
  struct sockaddr_in local_addr, peer_addr;
  SOCKET sock;
  int gres, lgres;
  int yes;
  int salen;

  memset(&hints, '\0', sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  gres = getaddrinfo(remote_host, remote_port, &hints, &res);
  if (gres != 0) {
    snprintf(errbuf, errbuf_len, "%s", gai_strerrorA(gres));
    return (false);
  }
  memset(&local_hints, '\0', sizeof(local_hints));
  local_hints.ai_family = AF_INET;
  local_hints.ai_socktype = SOCK_DGRAM;
  local_hints.ai_protocol = IPPROTO_UDP;
  if (local_host[0] == '\0')
    local_hints.ai_flags = AI_PASSIVE;
  lgres = getaddrinfo(local_host[0] != '\0' ? local_host : NULL, local_port,
    &local_hints, &lres);
  if (lgres != 0) {
    snprintf(errbuf, errbuf_len, "%s", gai_strerrorA(lgres));
    freeaddrinfo(res);
    return (false);
  }
  sock = INVALID_SOCKET;
  for (rp = res; rp != NULL; rp = rp->ai_next) {
    for (lrp = lres; lrp != NULL; lrp = lrp->ai_next) {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sock == INVALID_SOCKET)
        continue;
      yes = 1;
      (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
        sizeof(yes));
      if (bind(sock, lrp->ai_addr, (int)lrp->ai_addrlen) != 0) {
        closesocket(sock);
        sock = INVALID_SOCKET;
        continue;
      }
      if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) != 0) {
        closesocket(sock);
        sock = INVALID_SOCKET;
        continue;
      }
      break;
    }
    if (sock != INVALID_SOCKET)
      break;
  }
  freeaddrinfo(res);
  freeaddrinfo(lres);
  if (sock == INVALID_SOCKET) {
    snprintf(errbuf, errbuf_len,
      "unable to bind/connect UDP local %s%s%s remote %s:%s",
      local_host[0] != '\0' ? local_host : "0.0.0.0",
      local_port[0] != '\0' ? ":" : "", local_port, remote_host, remote_port);
    return (false);
  }
  if (!set_nonblocking_socket(sock, errbuf, errbuf_len)) {
    closesocket(sock);
    return (false);
  }
  salen = (int)sizeof(local_addr);
  memset(&local_addr, '\0', sizeof(local_addr));
  memset(&peer_addr, '\0', sizeof(peer_addr));
  if (getsockname(sock, (struct sockaddr *)&local_addr, &salen) == 0)
    format_sockaddr(&local_addr, local_desc, local_desc_len);
  else
    snprintf(local_desc, local_desc_len, "<unknown>");
  salen = (int)sizeof(peer_addr);
  if (getpeername(sock, (struct sockaddr *)&peer_addr, &salen) == 0)
    format_sockaddr(&peer_addr, peer_desc, peer_desc_len);
  else
    snprintf(peer_desc, peer_desc_len, "<unknown>");
  *sockp = (netsim_socket_t)sock;
  return (true);
}

bool
netsim_socket_open_bound_udp(const char *local_host, const char *local_port,
  netsim_socket_t *sockp, char *local_desc, size_t local_desc_len,
  char *errbuf, size_t errbuf_len)
{
  struct sockaddr_in local_addr, bound_addr;
  SOCKET sock;
  int yes;
  int salen;

  if (!resolve_udp_addr(local_host, local_port, true, &local_addr, errbuf,
        errbuf_len))
    return (false);
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  yes = 1;
  (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
    sizeof(yes));
  if (bind(sock, (const struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    closesocket(sock);
    return (false);
  }
  if (!set_socket_recv_timeout(sock, 100, errbuf, errbuf_len)) {
    closesocket(sock);
    return (false);
  }
  salen = (int)sizeof(bound_addr);
  memset(&bound_addr, '\0', sizeof(bound_addr));
  if (getsockname(sock, (struct sockaddr *)&bound_addr, &salen) == 0)
    format_sockaddr(&bound_addr, local_desc, local_desc_len);
  else
    snprintf(local_desc, local_desc_len, "<unknown>");
  *sockp = (netsim_socket_t)sock;
  return (true);
}

void
netsim_socket_close(netsim_socket_t sock)
{

  if (sock != NETSIM_SOCKET_INVALID)
    closesocket((SOCKET)sock);
}

int
netsim_socket_send(netsim_socket_t sock, const void *buf, size_t len)
{

  return ((int)send((SOCKET)sock, (const char *)buf, (int)len, 0));
}

int
netsim_socket_recv(netsim_socket_t sock, void *buf, size_t len)
{

  return ((int)recv((SOCKET)sock, (char *)buf, (int)len, 0));
}

int
netsim_socket_sendto(netsim_socket_t sock, const void *buf, size_t len,
  const netsim_sockaddr_t *addrp)
{

  return ((int)sendto((SOCKET)sock, (const char *)buf, (int)len, 0,
    (const struct sockaddr *)&addrp->ss, (int)addrp->len));
}

int
netsim_socket_recvfrom(netsim_socket_t sock, void *buf, size_t len,
  netsim_sockaddr_t *addrp)
{
  int salen;
  int rval;

  salen = (int)sizeof(addrp->ss);
  memset(addrp, '\0', sizeof(*addrp));
  rval = (int)recvfrom((SOCKET)sock, (char *)buf, (int)len, 0,
    (struct sockaddr *)&addrp->ss, &salen);
  if (rval >= 0)
    addrp->len = (socklen_t)salen;
  return (rval);
}

bool
netsim_socket_getsockname(netsim_socket_t sock, netsim_sockaddr_t *addrp)
{
  int salen;

  salen = (int)sizeof(addrp->ss);
  memset(addrp, '\0', sizeof(*addrp));
  if (getsockname((SOCKET)sock, (struct sockaddr *)&addrp->ss, &salen) != 0)
    return (false);
  addrp->len = (socklen_t)salen;
  return (true);
}

bool
netsim_sockaddr_resolve_udp(const char *host, const char *port,
  netsim_sockaddr_t *addrp, char *errbuf, size_t errbuf_len)
{
  struct sockaddr_in sin;

  if (!resolve_udp_addr(host, port, false, &sin, errbuf, errbuf_len))
    return (false);
  memset(addrp, '\0', sizeof(*addrp));
  memcpy(&addrp->ss, &sin, sizeof(sin));
  addrp->len = sizeof(sin);
  return (true);
}

bool
netsim_sockaddr_local_for_peer(const char *peer_host, const char *peer_port,
  char *hostbuf, size_t hostbuf_len, char *errbuf, size_t errbuf_len)
{
  struct sockaddr_in peer_addr, local_addr;
  SOCKET sock;
  int salen;

  if (!resolve_udp_addr(peer_host, peer_port, false, &peer_addr, errbuf,
        errbuf_len))
    return (false);
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  if (connect(sock, (const struct sockaddr *)&peer_addr, sizeof(peer_addr)) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    closesocket(sock);
    return (false);
  }
  salen = (int)sizeof(local_addr);
  if (getsockname(sock, (struct sockaddr *)&local_addr, &salen) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    closesocket(sock);
    return (false);
  }
  closesocket(sock);
  if (inet_ntop(AF_INET, &local_addr.sin_addr, hostbuf, (DWORD)hostbuf_len) == NULL) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  return (true);
}

bool
netsim_sockaddr_format(const netsim_sockaddr_t *addrp, char *buf, size_t buflen)
{

  if (addrp == NULL || addrp->len < sizeof(struct sockaddr_in) ||
      ((const struct sockaddr *)&addrp->ss)->sa_family != AF_INET)
    return (false);
  format_sockaddr((const struct sockaddr_in *)&addrp->ss, buf, buflen);
  return (true);
}

bool
netsim_sockaddr_same(const netsim_sockaddr_t *ap, const netsim_sockaddr_t *bp)
{
  const struct sockaddr_in *sa, *sb;

  if (ap == NULL || bp == NULL || ap->len < sizeof(*sa) || bp->len < sizeof(*sb))
    return (false);
  sa = (const struct sockaddr_in *)&ap->ss;
  sb = (const struct sockaddr_in *)&bp->ss;
  return (sa->sin_family == sb->sin_family &&
    sa->sin_port == sb->sin_port &&
    sa->sin_addr.s_addr == sb->sin_addr.s_addr);
}

int
netsim_socket_last_error(void)
{

  return (WSAGetLastError());
}

bool
netsim_socket_err_wouldblock(int err)
{

  return (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT);
}

bool
netsim_socket_err_transient(int err)
{

  switch (err) {
    case WSAECONNREFUSED:
    case WSAEHOSTDOWN:
    case WSAEHOSTUNREACH:
    case WSAENETDOWN:
    case WSAENETUNREACH:
    case WSAENOBUFS:
      return (true);
  }
  return (false);
}

const char *
netsim_socket_strerror(int err, char *buf, size_t buflen)
{
  DWORD flags;
  DWORD len;

  flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
    FORMAT_MESSAGE_MAX_WIDTH_MASK;
  len = FormatMessageA(flags, NULL, (DWORD)err, 0, buf, (DWORD)buflen, NULL);
  if (len == 0) {
    snprintf(buf, buflen, "Winsock error %d", err);
    return (buf);
  }
  while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
    buf[--len] = '\0';
  return (buf);
}

#else

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static uint64_t
netsim_now_ms(void)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return ((uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL);
}

bool
netsim_platform_init(void)
{

  return (true);
}

void
netsim_platform_cleanup(void)
{
}

void
netsim_mutex_init(netsim_mutex_t *mp)
{

  pthread_mutex_init(&mp->mutex, NULL);
}

void
netsim_mutex_destroy(netsim_mutex_t *mp)
{

  pthread_mutex_destroy(&mp->mutex);
}

void
netsim_mutex_lock(netsim_mutex_t *mp)
{

  pthread_mutex_lock(&mp->mutex);
}

void
netsim_mutex_unlock(netsim_mutex_t *mp)
{

  pthread_mutex_unlock(&mp->mutex);
}

void
netsim_cond_init(netsim_cond_t *cp)
{
  pthread_condattr_t attr;

  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(&cp->cond, &attr);
  pthread_condattr_destroy(&attr);
}

void
netsim_cond_destroy(netsim_cond_t *cp)
{

  pthread_cond_destroy(&cp->cond);
}

void
netsim_cond_wait(netsim_cond_t *cp, netsim_mutex_t *mp)
{

  pthread_cond_wait(&cp->cond, &mp->mutex);
}

bool
netsim_cond_timedwait(netsim_cond_t *cp, netsim_mutex_t *mp,
  netsim_deadline_t deadline)
{
  struct timespec ts;
  uint64_t secs;

  secs = deadline / 1000ULL;
  ts.tv_sec = (time_t)secs;
  ts.tv_nsec = (long)((deadline % 1000ULL) * 1000000ULL);
  return (pthread_cond_timedwait(&cp->cond, &mp->mutex, &ts) == 0);
}

void
netsim_cond_broadcast(netsim_cond_t *cp)
{

  pthread_cond_broadcast(&cp->cond);
}

bool
netsim_thread_create(netsim_thread_t *thrp, netsim_thread_fn fn, void *arg)
{

  return (pthread_create(thrp, NULL, fn, arg) == 0);
}

void
netsim_thread_join(netsim_thread_t thr)
{

  pthread_join(thr, NULL);
}

netsim_deadline_t
netsim_deadline_after_ms(int ms)
{

  return (netsim_now_ms() + (uint64_t)ms);
}

bool
netsim_deadline_due(netsim_deadline_t deadline)
{

  return (netsim_now_ms() >= deadline);
}

uint64_t
netsim_monotonic_ns(void)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return ((uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec);
}

uint32_t
netsim_process_id(void)
{

  return ((uint32_t)getpid());
}

void
netsim_sleep_ms(int ms)
{
  struct timespec ts;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
    ;
}

static void
format_sockaddr(const struct sockaddr_in *sap, char *buf, size_t buflen)
{
  char ipbuf[INET_ADDRSTRLEN];

  if (inet_ntop(AF_INET, &sap->sin_addr, ipbuf, sizeof(ipbuf)) == NULL) {
    strncpy(buf, "<unknown>", buflen);
    buf[buflen - 1] = '\0';
    return;
  }
  snprintf(buf, buflen, "%s:%u", ipbuf, (unsigned int)ntohs(sap->sin_port));
}

static bool
resolve_udp_addr(const char *host, const char *port, bool passive,
  struct sockaddr_in *sap, char *errbuf, size_t errbuf_len)
{
  struct addrinfo hints, *res;
  const char *rhost;
  int gres;

  memset(&hints, '\0', sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  if (passive)
    hints.ai_flags = AI_PASSIVE;
  rhost = (host != NULL && host[0] != '\0') ? host : NULL;
  gres = getaddrinfo(rhost, port, &hints, &res);
  if (gres != 0) {
    snprintf(errbuf, errbuf_len, "cannot resolve %s%s%s: %s",
      rhost != NULL ? rhost : "*", port != NULL ? ":" : "",
      port != NULL ? port : "", gai_strerror(gres));
    return (false);
  }
  memcpy(sap, res->ai_addr, sizeof(*sap));
  freeaddrinfo(res);
  return (true);
}

static bool
set_nonblocking_socket(int sock, char *errbuf, size_t errbuf_len)
{

  if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  return (true);
}

static bool
set_socket_recv_timeout(int sock, int timeout_ms, char *errbuf,
  size_t errbuf_len)
{
  struct timeval tv;

  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  return (true);
}

bool
netsim_socket_open_udp(const char *local_host, const char *local_port,
  const char *remote_host, const char *remote_port, netsim_socket_t *sockp,
  char *local_desc, size_t local_desc_len, char *peer_desc,
  size_t peer_desc_len, char *errbuf, size_t errbuf_len)
{
  struct addrinfo hints, local_hints, *res, *rp, *lres, *lrp;
  struct sockaddr_in local_addr, peer_addr;
  socklen_t salen;
  int sock, gres, lgres, one;

  memset(&hints, '\0', sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  gres = getaddrinfo(remote_host, remote_port, &hints, &res);
  if (gres != 0) {
    snprintf(errbuf, errbuf_len, "%s", gai_strerror(gres));
    return (false);
  }
  memset(&local_hints, '\0', sizeof(local_hints));
  local_hints.ai_family = AF_INET;
  local_hints.ai_socktype = SOCK_DGRAM;
  local_hints.ai_protocol = IPPROTO_UDP;
  if (local_host[0] == '\0')
    local_hints.ai_flags = AI_PASSIVE;
  lgres = getaddrinfo(local_host[0] != '\0' ? local_host : NULL, local_port,
    &local_hints, &lres);
  if (lgres != 0) {
    snprintf(errbuf, errbuf_len, "%s", gai_strerror(lgres));
    freeaddrinfo(res);
    return (false);
  }
  sock = -1;
  for (rp = res; rp != NULL; rp = rp->ai_next) {
    for (lrp = lres; lrp != NULL; lrp = lrp->ai_next) {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sock < 0)
        continue;
      one = 1;
      (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
      if (bind(sock, lrp->ai_addr, lrp->ai_addrlen) != 0) {
        close(sock);
        sock = -1;
        continue;
      }
      if (connect(sock, rp->ai_addr, rp->ai_addrlen) != 0) {
        close(sock);
        sock = -1;
        continue;
      }
      break;
    }
    if (sock >= 0)
      break;
  }
  freeaddrinfo(res);
  freeaddrinfo(lres);
  if (sock < 0) {
    snprintf(errbuf, errbuf_len,
      "unable to bind/connect UDP local %s%s%s remote %s:%s",
      local_host[0] != '\0' ? local_host : "0.0.0.0",
      local_port[0] != '\0' ? ":" : "", local_port, remote_host, remote_port);
    return (false);
  }
  if (!set_nonblocking_socket(sock, errbuf, errbuf_len)) {
    close(sock);
    return (false);
  }
  salen = sizeof(local_addr);
  memset(&local_addr, '\0', sizeof(local_addr));
  memset(&peer_addr, '\0', sizeof(peer_addr));
  if (getsockname(sock, (struct sockaddr *)&local_addr, &salen) == 0)
    format_sockaddr(&local_addr, local_desc, local_desc_len);
  else
    snprintf(local_desc, local_desc_len, "<unknown>");
  salen = sizeof(peer_addr);
  if (getpeername(sock, (struct sockaddr *)&peer_addr, &salen) == 0)
    format_sockaddr(&peer_addr, peer_desc, peer_desc_len);
  else
    snprintf(peer_desc, peer_desc_len, "<unknown>");
  *sockp = (netsim_socket_t)sock;
  return (true);
}

bool
netsim_socket_open_bound_udp(const char *local_host, const char *local_port,
  netsim_socket_t *sockp, char *local_desc, size_t local_desc_len,
  char *errbuf, size_t errbuf_len)
{
  struct sockaddr_in local_addr, bound_addr;
  socklen_t salen;
  int sock, one;

  if (!resolve_udp_addr(local_host, local_port, true, &local_addr, errbuf,
        errbuf_len))
    return (false);
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  one = 1;
  (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (bind(sock, (const struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    close(sock);
    return (false);
  }
  if (!set_socket_recv_timeout(sock, 100, errbuf, errbuf_len)) {
    close(sock);
    return (false);
  }
  salen = sizeof(bound_addr);
  memset(&bound_addr, '\0', sizeof(bound_addr));
  if (getsockname(sock, (struct sockaddr *)&bound_addr, &salen) == 0)
    format_sockaddr(&bound_addr, local_desc, local_desc_len);
  else
    snprintf(local_desc, local_desc_len, "<unknown>");
  *sockp = (netsim_socket_t)sock;
  return (true);
}

void
netsim_socket_close(netsim_socket_t sock)
{

  if (sock != NETSIM_SOCKET_INVALID)
    close((int)sock);
}

int
netsim_socket_send(netsim_socket_t sock, const void *buf, size_t len)
{

  return ((int)send((int)sock, buf, len, 0));
}

int
netsim_socket_recv(netsim_socket_t sock, void *buf, size_t len)
{

  return ((int)recv((int)sock, buf, len, 0));
}

int
netsim_socket_sendto(netsim_socket_t sock, const void *buf, size_t len,
  const netsim_sockaddr_t *addrp)
{

  return ((int)sendto((int)sock, buf, len, 0,
    (const struct sockaddr *)&addrp->ss, addrp->len));
}

int
netsim_socket_recvfrom(netsim_socket_t sock, void *buf, size_t len,
  netsim_sockaddr_t *addrp)
{
  socklen_t salen;
  int rval;

  salen = sizeof(addrp->ss);
  memset(addrp, '\0', sizeof(*addrp));
  rval = (int)recvfrom((int)sock, buf, len, 0,
    (struct sockaddr *)&addrp->ss, &salen);
  if (rval >= 0)
    addrp->len = salen;
  return (rval);
}

bool
netsim_socket_getsockname(netsim_socket_t sock, netsim_sockaddr_t *addrp)
{
  socklen_t salen;

  salen = sizeof(addrp->ss);
  memset(addrp, '\0', sizeof(*addrp));
  if (getsockname((int)sock, (struct sockaddr *)&addrp->ss, &salen) != 0)
    return (false);
  addrp->len = salen;
  return (true);
}

bool
netsim_sockaddr_resolve_udp(const char *host, const char *port,
  netsim_sockaddr_t *addrp, char *errbuf, size_t errbuf_len)
{
  struct sockaddr_in sin;

  if (!resolve_udp_addr(host, port, false, &sin, errbuf, errbuf_len))
    return (false);
  memset(addrp, '\0', sizeof(*addrp));
  memcpy(&addrp->ss, &sin, sizeof(sin));
  addrp->len = sizeof(sin);
  return (true);
}

bool
netsim_sockaddr_local_for_peer(const char *peer_host, const char *peer_port,
  char *hostbuf, size_t hostbuf_len, char *errbuf, size_t errbuf_len)
{
  struct sockaddr_in peer_addr, local_addr;
  socklen_t salen;
  int sock;

  if (!resolve_udp_addr(peer_host, peer_port, false, &peer_addr, errbuf,
        errbuf_len))
    return (false);
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  if (connect(sock, (const struct sockaddr *)&peer_addr, sizeof(peer_addr)) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    close(sock);
    return (false);
  }
  salen = sizeof(local_addr);
  if (getsockname(sock, (struct sockaddr *)&local_addr, &salen) != 0) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    close(sock);
    return (false);
  }
  close(sock);
  if (inet_ntop(AF_INET, &local_addr.sin_addr, hostbuf, hostbuf_len) == NULL) {
    netsim_socket_strerror(netsim_socket_last_error(), errbuf, errbuf_len);
    return (false);
  }
  return (true);
}

bool
netsim_sockaddr_format(const netsim_sockaddr_t *addrp, char *buf, size_t buflen)
{

  if (addrp == NULL || addrp->len < sizeof(struct sockaddr_in) ||
      ((const struct sockaddr *)&addrp->ss)->sa_family != AF_INET)
    return (false);
  format_sockaddr((const struct sockaddr_in *)&addrp->ss, buf, buflen);
  return (true);
}

bool
netsim_sockaddr_same(const netsim_sockaddr_t *ap, const netsim_sockaddr_t *bp)
{
  const struct sockaddr_in *sa, *sb;

  if (ap == NULL || bp == NULL || ap->len < sizeof(*sa) || bp->len < sizeof(*sb))
    return (false);
  sa = (const struct sockaddr_in *)&ap->ss;
  sb = (const struct sockaddr_in *)&bp->ss;
  return (sa->sin_family == sb->sin_family &&
    sa->sin_port == sb->sin_port &&
    sa->sin_addr.s_addr == sb->sin_addr.s_addr);
}

int
netsim_socket_last_error(void)
{

  return (errno);
}

bool
netsim_socket_err_wouldblock(int err)
{

  return (err == EAGAIN || err == EWOULDBLOCK);
}

bool
netsim_socket_err_transient(int err)
{

  switch (err) {
    case ECONNREFUSED:
    case EHOSTDOWN:
    case EHOSTUNREACH:
    case ENETDOWN:
    case ENETUNREACH:
    case ENOBUFS:
      return (true);
  }
  return (false);
}

const char *
netsim_socket_strerror(int err, char *buf, size_t buflen)
{

  (void)buflen;
  strncpy(buf, strerror(err), buflen);
  buf[buflen - 1] = '\0';
  return (buf);
}

#endif

#endif
