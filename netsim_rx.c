#include "netsim_rx.h"

#include <errno.h>
#include <stdlib.h>

void *
netsim_rx_thread(void *arg)
{
  struct netsim_rx_ctx *rxp;
  struct netsim_out_ev outev = {};
  netsim_sockaddr_t peer_addr;
  uint8_t *buf;
  int rlen, err;

  rxp = (struct netsim_rx_ctx *)arg;
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
