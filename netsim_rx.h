#ifndef NETSIM_RX_H
#define NETSIM_RX_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "def.h"
#include "netsim_platform.h"
#include "netsim_sip.h"

#define NETSIM_QUEUE_LEN 32
#define NETSIM_RX_BUFSIZE 4096

enum netsim_out_type {
  NETSIM_OUT_HELLO = 100,
  NETSIM_OUT_FRAME = 101,
  NETSIM_OUT_EXIT = 102,
  NETSIM_OUT_STOP = 103,
  NETSIM_OUT_START = 104,
  NETSIM_OUT_START_ACK = 105,
  NETSIM_OUT_SHUTDOWN = 106,
  NETSIM_INT_RX_PACKET = 107,
  NETSIM_INT_RX_ERROR = 108,
  NETSIM_INT_ABORT = 109
};

struct netsim_out_ev {
  int type;
  uint32_t frame;
  uint8_t bits;
  uint64_t nonce;
  char peer_user_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str peer_user;
  uint64_t recv_ns;
  int err;
  size_t pkt_len;
  netsim_sockaddr_t peer_addr;
  uint8_t *pkt_buf;
};

struct netsim_out_queue {
  netsim_mutex_t mutex;
  netsim_cond_t cond;
  struct netsim_out_ev items[NETSIM_QUEUE_LEN];
  size_t head;
  size_t tail;
  size_t len;
};

struct netsim_rx_ctx {
  netsim_socket_t sock;
  atomic_bool stop_requested;
  struct netsim_out_queue *outq;
};

void queue_out_put(struct netsim_out_queue *qp, const struct netsim_out_ev *evp);
void queue_out_wake(struct netsim_out_queue *qp);
void *netsim_rx_thread(void *arg);

#endif
