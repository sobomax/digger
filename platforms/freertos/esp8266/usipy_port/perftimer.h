#include "usipy_esp8266_timer1.h"

struct timer_opduration {
    uint32_t bts;
    uint32_t ets;
    const char *dunit;
};

static inline void
timer_opbegin(struct timer_opduration *odp)
{

    odp->bts = timer1_read();
}

static inline uint32_t
timer_opend(struct timer_opduration *odp)
{
    uint32_t r;

    odp->ets = timer1_read();
    if (odp->ets <= odp->bts) {
        r = odp->bts - odp->ets;
    } else {
        r = (uint32_t)T1VMAX - odp->ets + odp->bts + 1;
    }
    odp->dunit = "us";
    return (r / 80);
}
