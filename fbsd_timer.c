#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/fbio.h>
#include <sys/consio.h>
#include <vgl.h>

#include "def.h"
#include "fbsd_vid.h"
#include "digger_math.h"
#include "game.h"
#if defined(DIGGER_DEBUG)
#include "digger_log.h"
#endif

long int account = 0;
long int slept = 0;
int i = 0;

static struct PFD phase_detector;
static struct recfilter *loop_error;

#if defined(DIGGER_DEBUG)
static bool
gethrt_debug_enabled(void)
{
        static int cached = -1;

        if (cached == -1)
                cached = (getenv("DIGGER_GETHRT_DEBUG") != NULL) ? 1 : 0;
        return (cached != 0);
}
#endif

void inittimer(void)
{
        double tfreq;

	tfreq = 1000000.0 / dgstate.ftime;
	FIXME("inittimer called");
	loop_error = recfilter_init(tfreq, 0.1);
	PFD_init(&phase_detector, 0.0);
}

static double
timespec2dtime(time_t tv_sec, long tv_nsec)
{

    return (double)tv_sec + (double)tv_nsec / 1000000000.0;
}

static double
getdtime(void)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_UPTIME_PRECISE, &tp) == -1)
        return (-1);

    return timespec2dtime(tp.tv_sec, tp.tv_nsec);
}

void
gethrt(bool mindelay, int mult)
{
	uint32_t add_delay;
	double eval, clk_rl, tfreq, filterval, remote_lead_rl;

	VGLCheckSwitch();

	/* Speed controlling stuff */
	tfreq = mult * 1000000.0 / dgstate.ftime;
	clk_rl = getdtime() * tfreq;
	remote_lead_rl = (double)dgstate.netsim_remote_lead_ms * tfreq / 1000.0;
	clk_rl += remote_lead_rl;
	eval = PFD_get_error(&phase_detector, clk_rl);
	if (eval != 0.0) {
		filterval = recfilter_apply(loop_error, sigmoid(eval));
	} else {
                filterval = recfilter_getlast(loop_error);
        }
	add_delay = freqoff_to_period(tfreq, 1.0, filterval) * 1000000;
#if defined(DIGGER_DEBUG)
	if (gethrt_debug_enabled()) {
		digger_log_printf("gethrt: minsleep=%d mult=%d lead_ms=%d lead_rl=%f clk_rl=%f add_delay=%u eval=%f filterval=%f\n",
		    mindelay ? 1 : 0, mult, dgstate.netsim_remote_lead_ms, remote_lead_rl,
		    clk_rl, add_delay, eval, filterval);
	}
#endif
	doscreenupdate(false);
        usleep(add_delay);
	return;
}

int32_t getkips(void)
{
	FIXME("getkips called");
	return(0);
}

void s0soundoff(void)
{
	FIXME("s0soundoff called");
}

void s0setspkrt2(void)
{
	FIXME("s0setspkrt2 called");
}

void s0settimer0(int16_t t0v)
{
	FIXME("s0settimer0 called");
}

void s0settimer2(int16_t t0v)
{
	FIXME("s0settimer2 called");
}

void s0timer0(int16_t t0v)
{
	FIXME("s0timer0 called");
}

void s0timer2(int16_t t0v)
{
	FIXME("s0timer2 called");
}

void s0soundkillglob(void)
{
/* No-op */
}
