#include "netsim_instrument.h"
#include "netsim_rx.h"

#include <errno.h>
#include <stdlib.h>

#include "digger_log.h"

void *
netsim_rx_thread(void *arg)
{
  struct netsim_rx_ctx *rxp;
  struct netsim_out_ev outev = {0};
  netsim_sockaddr_t peer_addr;
#ifdef DIGGER_INSTRUMENTATION
  uint32_t drop_every;
  uint32_t recv_count;
#endif
  uint8_t *buf;
  int rlen, err;

  rxp = (struct netsim_rx_ctx *)arg;
#ifdef DIGGER_INSTRUMENTATION
  drop_every = netsim_rx_drop_every_env();
  recv_count = 0;
  if (drop_every != 0) {
    digger_log_printf("netsim-rx: synthetic loss enabled, dropping every %uth packet\n",
      (unsigned int)drop_every);
  }
#endif
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
#ifdef DIGGER_INSTRUMENTATION
    recv_count++;
    if (drop_every != 0 && recv_count % drop_every == 0) {
      digger_log_printf("netsim-rx: synthetic drop packet=%u len=%d\n",
        (unsigned int)recv_count, rlen);
      free(buf);
      continue;
    }
#endif
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
