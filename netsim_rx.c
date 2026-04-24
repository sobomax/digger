#include "netsim_rx.h"

#include <errno.h>
#include <stdlib.h>

#include "digger_log.h"

static uint32_t
netsim_rx_drop_every_env(void)
{
  const char *envp;
  char *endp;
  unsigned long v;

  envp = getenv("DIGGER_NETSIM_RX_DROP_EVERY");
  if (envp == NULL || envp[0] == '\0')
    return (0);
  v = strtoul(envp, &endp, 10);
  if (endp == envp || *endp != '\0' || v == 0 || v > UINT32_MAX) {
    digger_log_printf(
      "netsim-rx: ignoring invalid DIGGER_NETSIM_RX_DROP_EVERY=%s\n", envp);
    return (0);
  }
  return ((uint32_t)v);
}

void *
netsim_rx_thread(void *arg)
{
  struct netsim_rx_ctx *rxp;
  struct netsim_out_ev outev = {};
  netsim_sockaddr_t peer_addr;
  uint32_t drop_every;
  uint32_t recv_count;
  uint8_t *buf;
  int rlen, err;

  rxp = (struct netsim_rx_ctx *)arg;
  drop_every = netsim_rx_drop_every_env();
  recv_count = 0;
  if (drop_every != 0) {
    digger_log_printf("netsim-rx: synthetic loss enabled, dropping every %uth packet\n",
      (unsigned int)drop_every);
  }
  while (!atomic_load_explicit(&rxp->stop_requested, memory_order_relaxed)) {
    buf = malloc(NETSIM_RX_BUFSIZE);
    if (buf == NULL) {
      outev.type = NETSIM_INT_RX_ERROR;
      outev.err = ENOMEM;
      queue_out_put(rxp->outq, &outev);
      return (NULL);
    }
    rlen = netsim_socket_recvfrom(rxp->sock, buf, NETSIM_RX_BUFSIZE, &peer_addr);
    if (rlen < 0) {
      err = netsim_socket_last_error();
      free(buf);
      if (netsim_socket_err_wouldblock(err) || netsim_socket_err_transient(err))
        continue;
      outev.type = NETSIM_INT_RX_ERROR;
      outev.err = err;
      queue_out_put(rxp->outq, &outev);
      return (NULL);
    }
    recv_count++;
    if (drop_every != 0 && recv_count % drop_every == 0) {
      digger_log_printf("netsim-rx: synthetic drop packet=%u len=%d\n",
        (unsigned int)recv_count, rlen);
      free(buf);
      continue;
    }
    outev.type = NETSIM_INT_RX_PACKET;
    outev.recv_ns = netsim_monotonic_ns();
    outev.pkt_len = (size_t)rlen;
    outev.peer_addr = peer_addr;
    outev.pkt_buf = buf;
    queue_out_put(rxp->outq, &outev);
  }
  queue_out_wake(rxp->outq);
  return (NULL);
}
