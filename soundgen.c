#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct sgen_band {
    double freq;
    double amp;
    struct {
        double prd;
    } wrk;
};

struct sgen_state {
    uint64_t step;
    uint32_t srate;
    int nbands;
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
    ssp->bands[band].wrk.prd = (double)(ssp->srate) / freq;
}

struct pdres {
    uint64_t ipart;
    double fpart;
};

static void
precisediv(uint64_t x, uint64_t y, struct pdres *pdrp)
{

    pdrp->ipart = x / y;
    x -= pdrp->ipart * y;
    pdrp->fpart = (double)(x) / y;
}

int16_t
sgen_getsample(struct sgen_state *ssp)
{
    int32_t osample;
    int i;
    uint64_t pos;

    osample = 0;
    pos = (ssp->step << 32) / ssp->srate;
    for (i = 0; i < ssp->nbands; i++) {
        struct sgen_band *sbp;
        uint64_t cpos;

        sbp = &ssp->bands[i];
#if 0
        cpos = pos % sbp->wrk.prd;
        cpos = ssp->step % (ssp->srate / sbp->freq);
#endif

        cpos = (uint64_t)(ssp->step * sbp->freq) / ssp->srate;
        cpos = ssp->step - (uint64_t)(cpos * ssp->srate) / sbp->freq;

        if (cpos < (sbp->wrk.prd / 2)) {
            osample += sbp->amp * INT16_MAX;
        } else {
            osample += -sbp->amp * INT16_MAX;
        }
    }
    osample /= ssp->nbands;
    ssp->step += 1;
    return (osample);
}

#include <assert.h>
#include <stdio.h>

//#define TEST_SRATE 44100
#define TEST_SRATE 384000
#define TEST_DUR   100

int
sgen_test(void)
{
    struct sgen_state *ssp;
    unsigned int i, npos, nneg, nzero;
    FILE *of;
    int16_t obuf[TEST_SRATE * TEST_DUR];

    npos = nneg = nzero = 0;
    ssp = sgen_ctor(TEST_SRATE, 2);
    assert(ssp != NULL);
    sgen_setband(ssp, 0, 1607, 1.0);
    sgen_setband(ssp, 1, 2087, 0.0);
    for (i = 0; i < TEST_SRATE * TEST_DUR; i++) {
        obuf[i] = sgen_getsample(ssp);
        if (obuf[i] == 0) {
            nzero++;
        } else if (obuf[i] > 0) {
            npos++;
        } else {
            nneg++;
        }
    }
    of = fopen("sgen_test.out", "w");
    assert(of != NULL);
    assert(fwrite(obuf, TEST_SRATE * TEST_DUR, 1, of) == 1);
    assert(fclose(of) == 0);
    printf("nzero=%u npos=%u nneg=%u\n", nzero, npos, nneg);
    return (0);
}
