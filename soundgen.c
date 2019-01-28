/*
 * Copyright (c) 2019 Sippy Software, Inc., http://www.sippysoft.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "soundgen.h"

struct pdres {
    uint64_t ires;
    uint64_t nres;
    uint64_t irem;
    double frem;
};

enum band_types {BND_GEN, BND_MOD};

struct sgen_band {
    enum band_types b_type;
    double freq;
    double amp;
    struct {
        double prd;
        uint64_t lastspos;
        int16_t lut[2];
	int disabled;
    } wrk;
    int muted;
};

struct sgen_state {
    uint64_t step;
    uint32_t srate;
    int nbands;
    struct {
        uint64_t lastipos;
        uint64_t lastnpos;
    } wrk;
    struct sgen_band bands[];
};

struct sgen_state *
sgen_ctor(uint32_t srate, int nbands)
{
    struct sgen_state *ssp;
    size_t storsize;
    int i;

    storsize = sizeof(*ssp) + (nbands * sizeof(ssp->bands[0]));
    ssp = malloc(storsize);
    if (ssp == NULL)
        return (NULL);
    memset(ssp, '\0', storsize);
    ssp->srate = srate;
    ssp->nbands = nbands;
    for (i = 0; i < nbands; i++) {
        ssp->bands[i].wrk.disabled = 1;
    }
    return (ssp);
}

void
sgen_dtor(struct sgen_state *ssp)
{

    free(ssp);
}

#include <assert.h>

void
sgen_setband(struct sgen_state *ssp, int band, double freq, double amp)
{
    struct sgen_band *sbp;

    sbp = &ssp->bands[band];
    sbp->b_type = BND_GEN;
    sbp->freq = freq;
    sbp->amp = amp;
    assert(signbit(freq) == 0);
    if (freq > 0.0 && amp > 0.0) {
        sbp->wrk.prd = 1.0 / freq;
        sbp->wrk.lut[0] = amp * INT16_MAX;
        sbp->wrk.lut[1] = -amp * INT16_MAX;
	sbp->wrk.disabled = 0;
    } else {
        sbp->wrk.disabled = 1;
    }
}

void
sgen_setband_mod(struct sgen_state *ssp, int band, double freq, double a0, double a1)
{
    struct sgen_band *sbp;

    sbp = &ssp->bands[band];
    sbp->b_type = BND_MOD;
    sbp->freq = freq;
    sbp->amp = a1 - a0;
    assert(signbit(freq) == 0);
    if (freq > 0.0) {
        sbp->wrk.prd = 1.0 / freq;
        sbp->wrk.lut[0] = a0 * INT16_MAX;
        sbp->wrk.lut[1] = a1 * INT16_MAX;
        sbp->wrk.disabled = 0;
    } else {
        sbp->wrk.disabled = 1;
    }
}

int
sgen_setmuteband(struct sgen_state *ssp, int band, int muted)
{
    int rval;
    struct sgen_band *sbp;

    sbp = &ssp->bands[band];
    rval = sbp->muted;
    sbp->muted = muted;
    return (rval);
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
precisedivf(const struct pdres *xp, double y, struct pdres *pdrp)
{
    double res, nres;

    pdrp->ires = trunc(xp->ires / y);
    nres = pdrp->ires * y;
    pdrp->nres = trunc(nres);
    res = xp->ires - nres;
    pdrp->frem = fmod(res + xp->frem, y);
}

int16_t
sgen_getsample(struct sgen_state *ssp)
{
    int32_t osample;
    int32_t omod;
    int i, j;
    struct pdres pos;

    osample = 0;
    omod = INT16_MAX;
    precisediv(ssp->step - ssp->wrk.lastnpos, ssp->srate, &pos);
    ssp->wrk.lastnpos += pos.nres;
    pos.ires += ssp->wrk.lastipos;
    for (i = 0; i < ssp->nbands; i++) {
        struct sgen_band *sbp;
        struct pdres cpos, tpos;

        sbp = &ssp->bands[i];
        if (sbp->wrk.disabled || sbp->muted)
            continue;
        tpos = pos;
        tpos.ires -= sbp->wrk.lastspos;
        precisedivf(&tpos, sbp->wrk.prd, &cpos);
        if (cpos.nres > 0)
            sbp->wrk.lastspos += cpos.nres;

        if ((cpos.frem * 2) < sbp->wrk.prd) {
	    j = 0;
	} else {
	    j = 1;
	}
        if (sbp->b_type == BND_GEN) {
            osample += sbp->wrk.lut[j];
	} else {
	    omod *= sbp->wrk.lut[j];
	    omod /= INT16_MAX;
	}
    }
    osample /= ssp->nbands;
    if (omod != INT16_MAX) {
        osample = (osample * omod) / INT16_MAX;
    }
    ssp->step += 1;
    ssp->wrk.lastipos = pos.ires;
    return (osample);
}

#if defined(sgen_test)
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
    int16_t *obuf;

    obuf = malloc(TEST_SRATE * TEST_DUR * sizeof(obuf[0]));
    assert(obuf != NULL);
    ssp = sgen_ctor(TEST_SRATE, 2);
    assert(ssp != NULL);
    sgen_setband(ssp, 0, 1607.0, 1.0);
    //sgen_setband(ssp, 0, 1.0 / 3.0, 1.0);
    //sgen_setband(ssp, 1, 2087, 0.0);
    sgen_setband_mod(ssp, 1, 3.0, 0.1, 1.0);
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
#endif
