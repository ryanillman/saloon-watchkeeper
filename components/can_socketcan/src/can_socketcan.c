#include "can_socketcan.h"
#include <errno.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

struct can_socketcan {
  int fd;
  bool bus_off;
};

bool can_socketcan_convert(const struct can_frame *cf, n2k_frame_t *out) {
  if (!(cf->can_id & CAN_EFF_FLAG) || (cf->can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG))) return false;
  out->id = cf->can_id & CAN_EFF_MASK;
  out->len = cf->len > 8 ? 8 : cf->len;
  memcpy(out->data, cf->data, out->len);
  return true;
}

can_socketcan_t *can_socketcan_open(const char *ifname) {
  int fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
  if (fd < 0) return NULL;
  struct ifreq ifr;
  memset(&ifr, 0, sizeof ifr);
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
    int e = errno;
    close(fd);
    errno = e;
    return NULL;
  }
  can_err_mask_t mask = CAN_ERR_BUSOFF;
  setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &mask, sizeof mask);
  struct sockaddr_can addr;
  memset(&addr, 0, sizeof addr);
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    int e = errno;
    close(fd);
    errno = e;
    return NULL;
  }
  can_socketcan_t *c = calloc(1, sizeof *c);
  if (!c) {
    close(fd);
    errno = ENOMEM;
    return NULL;
  }
  c->fd = fd;
  return c;
}

void can_socketcan_close(can_socketcan_t *c) {
  if (!c) return;
  close(c->fd);
  free(c);
}

static bool ops_recv(n2k_frame_t *f, void *ctx) {
  can_socketcan_t *c = ctx;
  struct can_frame cf;
  for (;;) {
    ssize_t n = read(c->fd, &cf, sizeof cf);
    if (n != (ssize_t)sizeof cf) return false;  // EAGAIN: nothing waiting
    if (cf.can_id & CAN_ERR_FLAG) {
      if (cf.can_id & CAN_ERR_BUSOFF) c->bus_off = true;
      continue;
    }
    if (can_socketcan_convert(&cf, f)) {
      c->bus_off = false;
      return true;
    }
  }
}

static bool ops_send(const n2k_frame_t *f, void *ctx) {
  can_socketcan_t *c = ctx;
  struct can_frame cf;
  memset(&cf, 0, sizeof cf);
  cf.can_id = (f->id & CAN_EFF_MASK) | CAN_EFF_FLAG;
  cf.len = f->len > 8 ? 8 : f->len;
  memcpy(cf.data, f->data, cf.len);
  return write(c->fd, &cf, sizeof cf) == (ssize_t)sizeof cf;
}

static n2k_can_state_t ops_state(void *ctx) { return ((can_socketcan_t *)ctx)->bus_off ? N2K_CAN_BUS_OFF : N2K_CAN_OK; }
// The kernel and the adapter handle the real recovery; clearing the flag lets the bus loop read again, and a
// bus that is still off simply sets it again with the next error frame.
static void ops_recover(void *ctx) { ((can_socketcan_t *)ctx)->bus_off = false; }

n2k_can_ops_t can_socketcan_ops(can_socketcan_t *c) {
  return (n2k_can_ops_t){ops_recv, ops_send, ops_state, ops_recover, c};
}
