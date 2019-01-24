#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct pdres {
    uint64_t ires;
    uint64_t nres;
    uint64_t irem;
    double frem;
};

struct sgen_band {
    double freq;
    double amp;
    struct {
        double prd;
        struct pdres lastpos;
    } wrk;
};

struct sgen_state {
    uint64_t step;
    uint32_t srate;
    int nbands;
    struct {
        struct pdres lastpos;
    } wrk;
    struct sgen_band bands[];
};

struct sgen_state *
sgen_ctor(uint32_t srate, int nbands)
{
    struct sgen_state *ssp;
    size_t storsize;

    storsize = sizeof(*ssp) + (nbands * sizeof(ssp->bands[0]));
    ssp = malloc(storsize);
    if (ssp == NULL)
        return (NULL);
    memset(ssp, '\0', storsize);
    ssp->srate = srate;
    ssp->nbands = 2;
    return (ssp);
}

void
sgen_setband(struct sgen_state *ssp, int band, double freq, double amp)
{

    ssp->bands[band].freq = freq;
    ssp->bands[band].amp = amp;
    ssp->bands[band].wrk.prd = 1 / freq;
}

static void
precisediv(uint64_t x, uint64_t y, struct pdres *pdrp)
{

    pdrp->ires = x / y;
    pdrp->nres = pdrp->ires * y;
    pdrp->irem = x - pdrp->nres;
    pdrp->frem = (double)(pdrp->irem) / y;
}

static void
precisedivf(struct pdres *xp, double y, struct pdres *pdrp)
{
    double res, nres;

    pdrp->ires = trunc(xp->ires / y);
    nres = pdrp->ires * y;
    pdrp->nres = trunc(nres);
    res = xp->ires - nres;
    pdrp->frem = fmod(res + xp->frem, y);
}

static double
precisefmod(struct pdres *xp, double y)
{
    double res;

    res = xp->ires - (y * trunc(xp->ires / y));
    return(fmod(res + xp->frem, y));
}

int16_t
sgen_getsample(struct sgen_state *ssp)
{
    int32_t osample;
    int i;
    struct pdres pos;

    osample = 0;
    precisediv(ssp->step - ssp->wrk.lastpos.nres, ssp->srate, &pos);
    pos.nres += ssp->wrk.lastpos.nres;
    pos.ires += ssp->wrk.lastpos.ires;
    for (i = 0; i < ssp->nbands; i++) {
        struct sgen_band *sbp;
        struct pdres cpos, tpos;

        sbp = &ssp->bands[i];
#if 1
        tpos = pos;
        tpos.ires -= sbp->wrk.lastpos.nres;
        precisedivf(&tpos, sbp->wrk.prd, &cpos);
        cpos.ires += sbp->wrk.lastpos.ires;
        cpos.nres += sbp->wrk.lastpos.nres;
#else
        cpos.frem = precisefmod(&pos, sbp->wrk.prd) * 2;
#endif

        if ((cpos.frem * 2) < sbp->wrk.prd) {
            osample += sbp->amp * INT16_MAX;
        } else {
            osample += -sbp->amp * INT16_MAX;
        }
        sbp->wrk.lastpos = cpos;
    }
    osample /= ssp->nbands;
    ssp->step += 1;
    ssp->wrk.lastpos = pos;
    return (osample);
}

#include <assert.h>
#include <stdio.h>

//#define TEST_SRATE 44100
#define TEST_SRATE 384000
#define TEST_DUR   100

struct wavestats {
   unsigned int npos, nneg, nzero, ntrans;
   unsigned int posdur_min, posdur_max;
   unsigned int negdur_min, negdur_max;
};

int
sgen_test(void)
{
    struct sgen_state *ssp;
    unsigned int i, j;
    struct wavestats wstats, wstats_prev;
    double rfreq;
    FILE *of;
    int16_t obuf[TEST_SRATE * TEST_DUR];

    ssp = sgen_ctor(TEST_SRATE, 2);
    assert(ssp != NULL);
    sgen_setband(ssp, 0, 1607, 1.0);
    sgen_setband(ssp, 1, 2087, 0.0);
    for (j = 0; j < 64; j += 1) {
        ssp->step = ((uint64_t)1 << j) - 1;
        memset(&wstats, '\0', sizeof(wstats));
        memset(&wstats_prev, '\0', sizeof(wstats_prev));
        for (i = 0; i < TEST_SRATE * TEST_DUR; i++) {
            obuf[i] = sgen_getsample(ssp);
            if (obuf[i] == 0) {
                wstats.nzero++;
            } else if (obuf[i] > 0) {
                wstats.npos++;
            } else {
                wstats.nneg++;
            }
            if (i == 0 || obuf[i - 1] != obuf[i]) {
                if (wstats.ntrans > 2) {
                    if (obuf[i - 1] > obuf[i]) {
                        /* Falling edge */
                        unsigned int posdur;

                        posdur = wstats.npos - wstats_prev.npos;
                        if (posdur > wstats.posdur_max)
                            wstats.posdur_max = posdur;
                        if (wstats.posdur_min == 0 || posdur < wstats.posdur_min)
                           wstats.posdur_min = posdur;
                    } else {
                        /* Rising edge */
                        unsigned int negdur;

                        negdur = wstats.nneg - wstats_prev.nneg;
                        if (negdur > wstats.negdur_max)
                            wstats.negdur_max = negdur;
                        if (wstats.negdur_min == 0 || negdur < wstats.negdur_min)
                            wstats.negdur_min = negdur;
                    }
                }
                wstats_prev = wstats;
                wstats.ntrans++;
            }
        }
        of = fopen("sgen_test.out", "w");
        assert(of != NULL);
        assert(fwrite(obuf, TEST_SRATE * TEST_DUR, 1, of) == 1);
        assert(fclose(of) == 0);
        printf("nzero=%u npos=%u nneg=%u ntrans=%u\n", wstats.nzero, wstats.npos, wstats.nneg, wstats.ntrans);
        rfreq = wstats.ntrans / (double)(TEST_DUR << 1);
        printf("rfreq=%f\n", rfreq);
        printf("posdur_max=%u posdur_min=%u negdur_max=%u negdur_min=%d\n", wstats.posdur_max, wstats.posdur_min, wstats.negdur_max, wstats.negdur_min);
    }
    return (0);
}
