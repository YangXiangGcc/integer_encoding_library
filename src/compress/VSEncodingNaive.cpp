/*-----------------------------------------------------------------------------
 *  VSEncodingNaive.cpp - A naive implementation of VSEncoding
 *      This implementation is used by VSE-R.
 *
 *  Coding-Style:
 *      emacs) Mode: C, tab-width: 8, c-basic-offset: 8, indent-tabs-mode: nil
 *      vi) tabstop: 8, expandtab
 *
 *  Authors:
 *      Takeshi Yamamuro <linguin.m.s_at_gmail.com>
 *      Fabrizio Silvestri <fabrizio.silvestri_at_isti.cnr.it>
 *      Rossano Venturini <rossano.venturini_at_isti.cnr.it>
 *-----------------------------------------------------------------------------
 */

#include "compress/VSEncodingNaive.hpp"

#define VSENAIVE_LOGLEN         3
#define VSENAIVE_LOGLOG         3

#define VSENAIVE_LENS_LEN       (1 << VSENAIVE_LOGLEN)

using namespace opc;

static uint32_t __vsenaive_possLens[] = {
        1, 2, 4, 6, 8, 16, 32, 64
};

static uint32_t __vsenaive_possLogs[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 20, 32
};

static uint32_t __vsenaive_remapLogs[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 16, 16, 16,
        20, 20, 20, 20,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

static uint32_t __vsenaive_codeLens[] = {
        0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 0, 0, 0, 0, 0, 0, 5,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7
};

static uint32_t __vsenaive_codeLogs[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
        13, 13, 13, 13, 14, 14, 14, 14,
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
};

static VSEncoding __vsenaive(
                &__vsenaive_possLens[0], NULL,
                VSENAIVE_LENS_LEN, false);

void
VSEncodingNaive::encodeArray(uint32_t *in,
                uint32_t len, uint32_t *out, uint32_t &nvalue)
{
        if (len > MAXLEN)
                eoutput("Overflowed input length (CHECK: MAXLEN)");

        /* Compute logs of all numbers */
        vector<uint32_t>        logs;

        __init_vector(logs, len);
        for (uint32_t i = 0; i < len; i++)
                logs[i] = __vsenaive_remapLogs[1 + __get_msb(in[i])];

        /* Compute optimal partition */
        vector<uint32_t>        parts;

        __vsenaive.compute_OptPartition(logs,
                        VSENAIVE_LOGLEN + VSENAIVE_LOGLOG, parts);

        uint32_t numBlocks = parts.size() - 1;

    	/* Ready to write */ 
        uint32_t        maxB;
        BitsWriter      wt(out);

        for (uint32_t i = 0; i < numBlocks; i++) {
                /* Compute max B in the block */
                maxB = 0;

                for (uint32_t j = parts[i]; j < parts[i + 1]; j++) {
                        if (maxB < logs[j])
                                maxB = logs[j];
                }

                /* Writes the value of B and K */
                wt.bit_writer(__vsenaive_codeLogs[maxB], VSENAIVE_LOGLOG);
                wt.bit_writer(__vsenaive_codeLens[parts[i + 1] -
                                parts[i]], VSENAIVE_LOGLEN);

                for (uint32_t j = parts[i]; j < parts[i + 1]; j++)
                        wt.bit_writer(in[j], maxB);
        }

        wt.bit_flush(); 

        nvalue = wt.get_written();
}

void
VSEncodingNaive::decodeArray(uint32_t *in,
                uint32_t len, uint32_t *out, uint32_t nvalue)
{
        BitsReader      rd(in);

        uint32_t *end = out + nvalue;

        while (end > out) {
                uint32_t B = __vsenaive_possLogs[rd.bit_reader(VSENAIVE_LOGLOG)];
                uint32_t K = __vsenaive_possLens[rd.bit_reader(VSENAIVE_LOGLEN)];

                for (uint32_t i = 0; i < K; i++)
                        out[i] = (B != 0)? rd.bit_reader(B) : 0;

                out += K;
        }
}

