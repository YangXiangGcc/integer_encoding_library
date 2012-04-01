/*-----------------------------------------------------------------------------
 *  VSEncodingBlocks.cpp - A naive implementation of VSEncoding
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

#include "compress/VSEncodingBlocks.hpp"

#define VSEBLOCKS_LOGLEN        4
#define VSEBLOCKS_LOGLOG        4
#define VSEBLOCKS_LOGDESC       (VSEBLOCKS_LOGLEN + VSEBLOCKS_LOGLOG)

#define VSEBLOCKS_LENS_LEN      (1 << VSEBLOCKS_LOGLEN)
#define VSEBLOCKS_LOGS_LEN      (1 << VSEBLOCKS_LOGLOG)

using namespace opc;

#define __vseblocks_copy16(src, dest)   \
        __asm__ __volatile__(           \
                "movdqu %4, %%xmm0\n\t"         \
                "movdqu %5, %%xmm1\n\t"         \
                "movdqu %6, %%xmm2\n\t"         \
                "movdqu %7, %%xmm3\n\t"         \
                "movdqu %%xmm0, %0\n\t"         \
                "movdqu %%xmm1, %1\n\t"         \
                "movdqu %%xmm2, %2\n\t"         \
                "movdqu %%xmm3, %3\n\t"         \
                :"=m" (dest[0]), "=m" (dest[4]), "=m" (dest[8]), "=m" (dest[12])        \
                :"m" (src[0]), "m" (src[4]), "m" (src[8]), "m" (src[12])                \
                :"memory", "%xmm0", "%xmm1", "%xmm2", "%xmm3")
	    
#define __vseblocks_zero32(dest)        \
        __asm__ __volatile__(           \
                "pxor   %%xmm0, %%xmm0\n\t"     \
                "movdqu %%xmm0, %0\n\t"         \
                "movdqu %%xmm0, %1\n\t"         \
                "movdqu %%xmm0, %2\n\t"         \
                "movdqu %%xmm0, %3\n\t"         \
                "movdqu %%xmm0, %4\n\t"         \
                "movdqu %%xmm0, %5\n\t"         \
                "movdqu %%xmm0, %6\n\t"         \
                "movdqu %%xmm0, %7\n\t"         \
                :"=m" (dest[0]), "=m" (dest[4]), "=m" (dest[8]), "=m" (dest[12]) ,               \
                        "=m" (dest[16]), "=m" (dest[20]), "=m" (dest[24]), "=m" (dest[28])       \
                ::"memory", "%xmm0")

/* A set of unpacking functions */
static void __vseblocks_unpack1(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack2(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack3(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack4(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack5(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack6(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack7(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack8(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack9(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack10(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack11(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack12(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack16(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack20(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);
static void __vseblocks_unpack32(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);

/* A interface of unpacking functions above */
typedef void (*__vseblocks_unpacker)(uint32_t *__no_aliases__ out,
                uint32_t *in, uint32_t bs);

static __vseblocks_unpacker       __vseblocks_unpack[] = {
        NULL, __vseblocks_unpack1,
        __vseblocks_unpack2, __vseblocks_unpack3,
        __vseblocks_unpack4, __vseblocks_unpack5,
        __vseblocks_unpack6, __vseblocks_unpack7,
        __vseblocks_unpack8, __vseblocks_unpack9,
        __vseblocks_unpack10, __vseblocks_unpack11,
        __vseblocks_unpack12, __vseblocks_unpack16,
        __vseblocks_unpack20, __vseblocks_unpack32
};

/*
 * There is asymmetry between possible lenghts ofblocks
 * if they are formed by zeros or larger numbers. 
 */
static uint32_t __vseblocks_possLens[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};

static uint32_t __vseblocks_posszLens[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 32
};

static uint32_t __vseblocks_remapLogs[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 16, 16, 16,
        20, 20, 20, 20,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

static uint32_t __vseblocks_codeLogs[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13,
        14, 14, 14, 14,
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
};

static uint32_t __vseblocks_possLogs[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 20, 32
};

static VSEncoding __vseblocks(
                 &__vseblocks_possLens[0],
                 &__vseblocks_posszLens[0],
                 VSEBLOCKS_LENS_LEN, false);

void
VSEncodingBlocks::encodeVS(uint32_t len,
                uint32_t *in, uint32_t &size, uint32_t *out)
{
        uint32_t        maxB;
        uint32_t        numBlocks;

        if (len > MAXLEN)
                eoutput("Overflowed input length (CHECK: MAXLEN)");

        uint32_t *logs = new uint32_t[len];
        if (logs == NULL)
                eoutput("Can't allocate memory: logs");

        uint32_t *parts = new uint32_t[len + 1];
        if (parts == NULL)
                eoutput("Can't allocate memory: parts");

        /* Compute logs of all numbers */
        for (uint32_t i = 0; i < len; i++)
                logs[i] = __vseblocks_remapLogs[1 + __get_msb(in[i])];

        /* Compute optimal partition */
        __vseblocks.compute_OptPartition(logs, len,
                        VSEBLOCKS_LOGLEN + VSEBLOCKS_LOGLOG,
                        parts, numBlocks);

        /* countBlocksLogs[i] says how many blocks uses i bits */
        uint32_t        blockCur[VSEBLOCKS_LOGS_LEN];
        uint32_t        countBlocksLogs[VSEBLOCKS_LOGS_LEN];

        for (uint32_t i = 0; i < VSEBLOCKS_LOGS_LEN; i++) {
                countBlocksLogs[i] = 0;
                blockCur[i] = 0;
        }

    	/* Count number of occs of each log */
        for (uint32_t i = 0; i < numBlocks; i++) {
       		/* Compute max B in the block */
                maxB = 0;

                for (uint32_t j = parts[i]; j < parts[i + 1]; j++) {
                        if (maxB < logs[j])
                                maxB = logs[j];
                }

                countBlocksLogs[__vseblocks_codeLogs[maxB]] += parts[i + 1] - parts[i];
        }

        uint32_t ntotal = 0;

        for (uint32_t i = 1; i < VSEBLOCKS_LOGS_LEN; i++) {
                if (countBlocksLogs[i] > 0)
                        ntotal++;
        }

     	/* Write occs. zero is assumed to be present */
        BitsWriter wt(out);
        wt.bit_writer(ntotal, 32);

        for (uint32_t i = 1; i < VSEBLOCKS_LOGS_LEN; i++) {
                if (countBlocksLogs[i] > 0) {
                        wt.bit_writer(countBlocksLogs[i], 28);
                        wt.bit_writer(i, 4);
                }
        }

        /* Prepare arrays to store groups of elements */
        uint32_t        *blocks[VSEBLOCKS_LOGS_LEN];

        blocks[0] = 0;

        for (uint32_t i = 1; i < VSEBLOCKS_LOGS_LEN; i++) {
                if (countBlocksLogs[i] > 0) {
                        blocks[i] = new uint32_t[countBlocksLogs[i]];

                        if (blocks[i] == NULL)
                                eoutput("Can't allocate memory: blocks[]");
                } else {
                        blocks[i] = NULL;
                }
        }

    	/* Permute the elements based on their values of B */
        for (uint32_t i = 0; i < numBlocks; i++) {
       		/* Compute max B in the block */
                maxB = 0;

                for (uint32_t j = parts[i]; j < parts[i + 1]; j++) {
                        if (maxB < logs[j])
                                maxB = logs[j];
                }

                if (!maxB)
                        continue;

                for (uint32_t j = parts[i]; j < parts[i + 1]; j++) {
                        /* Save current element in its bucket */
                        blocks[__vseblocks_codeLogs[maxB]][
                                blockCur[__vseblocks_codeLogs[maxB]]] = in[j];
                        blockCur[__vseblocks_codeLogs[maxB]]++;
                }
        }

        /* Write each bucket ... keeping byte alligment */ 
        for (uint32_t i = 1; i < VSEBLOCKS_LOGS_LEN; i++) {
                for (uint32_t j = 0; j < countBlocksLogs[i]; j++)
                        wt.bit_writer(blocks[i][j], __vseblocks_possLogs[i]);

                /* Align to next word */
                if (countBlocksLogs[i] > 0)
                        wt.bit_flush();
        }

    	/* write block codes... a byte each */
        for (uint32_t i = 0; i < numBlocks; i++) {
                /* Compute max B in the block */
                maxB = 0;

                for (uint32_t j = parts[i]; j < parts[i + 1]; j++) {
                        if (maxB < logs[j])
                                maxB = logs[j];
                }

                uint32_t idx = 0;

                if (maxB) {
       			/* Compute the code for the block length */
                        for (; idx < VSEBLOCKS_LENS_LEN; idx++) {
                                if (parts[i + 1] - parts[i] == __vseblocks_possLens[idx])
                                        break;
                        }
                } else {
                        /*
                         * Treat runs of 0 in a different way.
                         * Compute the code for the block length.
                         */
                        for (; idx < VSEBLOCKS_LENS_LEN; idx++) {
                                if (parts[i + 1] - parts[i] == __vseblocks_posszLens[idx])
                                        break;
                        }
                }

                /* Writes the value of B and K */
                wt.bit_writer(__vseblocks_codeLogs[maxB], VSEBLOCKS_LOGLOG);
                wt.bit_writer(idx, VSEBLOCKS_LOGLEN);
        }

        /* Align to 32-bit */
        wt.bit_flush(); 

        size = wt.get_written();

        /* Finalization */
        for (uint32_t i = 0; i < VSEBLOCKS_LOGS_LEN; i++)
                delete[] blocks[i]; 

        delete[] parts;
        delete[] logs;
}

void
VSEncodingBlocks::decodeVS(uint32_t len,
                uint32_t *in, uint32_t *out, uint32_t *aux)
{
        uint32_t        B;
        uint32_t        K;
        uint32_t        *pblk[VSEBLOCKS_LOGS_LEN];

        __validate(in, len);

        int ntotal = *in++;
        uint32_t *addr = in + ntotal;

        while (ntotal-- > 0) {
                B = (*in) & (VSEBLOCKS_LOGS_LEN - 1);
                uint32_t nblk = *(in++) >> VSEBLOCKS_LOGLEN;

                /* Do unpacking */
                (__vseblocks_unpack[B])(aux, addr, nblk);

                pblk[B] = aux;
                aux += nblk;
                addr += (nblk * __vseblocks_possLogs[B] + 31) / 32;
        }

        /*
         * FIXME: We assume that a 32-bit block is processed in a loop here.
         * I might think some amount of 32-bit blocks are processed simutaneously.
         */
        uint32_t *end = out + len;

        do {
                /* Permuting integers with a first 8-bit */
                B = (*addr) >> (VSEBLOCKS_LOGDESC * 3 + VSEBLOCKS_LOGLEN);
                K = (((*addr) >> (VSEBLOCKS_LOGDESC * 3)) & (VSEBLOCKS_LENS_LEN - 1));

                if (B) {
                        /*
                         * NOTICE: If a max value in __vseblocks_possLens[] is
                         * over 16, a code below needs to be fixed.
                         * __vseblocks_copy16() just copies 16 values each.
                         */
                        __vseblocks_copy16(pblk[B], out);
                        pblk[B] += __vseblocks_possLens[K];
                        out += __vseblocks_possLens[K];
                } else {
                        /* FIXME: Is it better to do memcpy() firstly? */
                        __vseblocks_zero32(out);
                        out += __vseblocks_posszLens[K];
                }

                /* Permuting integers with a second 8-bit */
                B = ((*addr) >> (VSEBLOCKS_LOGDESC * 2 + VSEBLOCKS_LOGLEN)) & (VSEBLOCKS_LOGS_LEN - 1);
                K = (((*addr) >> (VSEBLOCKS_LOGDESC * 2)) & (VSEBLOCKS_LENS_LEN - 1));

                if (B) {
                        __vseblocks_copy16(pblk[B], out);
                        pblk[B] += __vseblocks_possLens[K];
                        out += __vseblocks_possLens[K];
                } else {
                        __vseblocks_zero32(out);
                        out += __vseblocks_posszLens[K];
                }

                /* Permuting integers with a third 8-bit */
                B = ((*addr) >> (VSEBLOCKS_LOGDESC  + VSEBLOCKS_LOGLEN)) & (VSEBLOCKS_LOGS_LEN - 1);
                K = (((*addr) >> VSEBLOCKS_LOGDESC) & (VSEBLOCKS_LENS_LEN - 1));

                if (B) {
                        __vseblocks_copy16(pblk[B], out);
                        pblk[B] += __vseblocks_possLens[K];
                        out += __vseblocks_possLens[K];
                } else {
                        __vseblocks_zero32(out);
                        out += __vseblocks_posszLens[K];
                }

                /* Permuting integers with a fourth 8-bit */
                B = ((*addr) >> VSEBLOCKS_LOGLEN) & (VSEBLOCKS_LOGS_LEN - 1);
                K = (*addr++) & (VSEBLOCKS_LENS_LEN - 1);

                if (B) {
                        __vseblocks_copy16(pblk[B], out);
                        pblk[B] += __vseblocks_possLens[K];
                        out += __vseblocks_possLens[K];
                } else {
                        __vseblocks_zero32(out);
                        out += __vseblocks_posszLens[K];
                }
        } while (end > out);
}

void
VSEncodingBlocks::encodeArray(uint32_t *in,
                uint32_t len, uint32_t *out, uint32_t &nvalue)
{
        uint32_t        res;
        uint32_t        *lin;
        uint32_t        *lout;
        uint32_t        csize;

        for (nvalue = 0, res = len, lin = in, lout = out; 
                        res > VSENCODING_BLOCKSZ;
                        res -= VSENCODING_BLOCKSZ, lin += VSENCODING_BLOCKSZ,
                        lout += csize, nvalue += csize + 1) {
                encodeVS(VSENCODING_BLOCKSZ, lin, csize, ++lout);
                *(lout - 1) = csize;
        }

        encodeVS(res, lin, csize, lout);
        nvalue += csize;
}

void
VSEncodingBlocks::decodeArray(uint32_t *in,
                uint32_t len, uint32_t *out, uint32_t nvalue)
{
        uint32_t        res;
        uint32_t        sum;

        __validate(in, (len << 2));
        __validate(out, ((nvalue + TAIL_MERGIN) << 2));

        for (res = nvalue; res > VSENCODING_BLOCKSZ;
                        out += VSENCODING_BLOCKSZ, in += sum,
                        res -= VSENCODING_BLOCKSZ) {
                sum = *in++;
                decodeVS(VSENCODING_BLOCKSZ, in, out, __vsencoding_aux);
        }

        decodeVS(res, in, out, __vsencoding_aux);
}

/* --- Intra functions below --- */

void
__vseblocks_unpack1(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 1) {
                out[0] = in[0] >> 31;
                out[1] = (in[0] >> 30) & 0x01;
                out[2] = (in[0] >> 29) & 0x01;
                out[3] = (in[0] >> 28) & 0x01;
                out[4] = (in[0] >> 27) & 0x01;
                out[5] = (in[0] >> 26) & 0x01;
                out[6] = (in[0] >> 25) & 0x01;
                out[7] = (in[0] >> 24) & 0x01;
                out[8] = (in[0] >> 23) & 0x01;
                out[9] = (in[0] >> 22) & 0x01;
                out[10] = (in[0] >> 21) & 0x01;
                out[11] = (in[0] >> 20) & 0x01;
                out[12] = (in[0] >> 19) & 0x01;
                out[13] = (in[0] >> 18) & 0x01;
                out[14] = (in[0] >> 17) & 0x01;
                out[15] = (in[0] >> 16) & 0x01;
                out[16] = (in[0] >> 15) & 0x01;
                out[17] = (in[0] >> 14) & 0x01;
                out[18] = (in[0] >> 13) & 0x01;
                out[19] = (in[0] >> 12) & 0x01;
                out[20] = (in[0] >> 11) & 0x01;
                out[21] = (in[0] >> 10) & 0x01;
                out[22] = (in[0] >> 9) & 0x01;
                out[23] = (in[0] >> 8) & 0x01;
                out[24] = (in[0] >> 7) & 0x01;
                out[25] = (in[0] >> 6) & 0x01;
                out[26] = (in[0] >> 5) & 0x01;
                out[27] = (in[0] >> 4) & 0x01;
                out[28] = (in[0] >> 3) & 0x01;
                out[29] = (in[0] >> 2) & 0x01;
                out[30] = (in[0] >> 1) & 0x01;
                out[31] = in[0] & 0x01;
        }
}

void
__vseblocks_unpack2(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 2) {
                out[0] = in[0] >> 30;
                out[1] = (in[0] >> 28) & 0x03;
                out[2] = (in[0] >> 26) & 0x03;
                out[3] = (in[0] >> 24) & 0x03;
                out[4] = (in[0] >> 22) & 0x03;
                out[5] = (in[0] >> 20) & 0x03;
                out[6] = (in[0] >> 18) & 0x03;
                out[7] = (in[0] >> 16) & 0x03;
                out[8] = (in[0] >> 14) & 0x03;
                out[9] = (in[0] >> 12) & 0x03;
                out[10] = (in[0] >> 10) & 0x03;
                out[11] = (in[0] >> 8) & 0x03;
                out[12] = (in[0] >> 6) & 0x03;
                out[13] = (in[0] >> 4) & 0x03;
                out[14] = (in[0] >> 2) & 0x03;
                out[15] = in[0] & 0x03;
                out[16] = in[1] >> 30;
                out[17] = (in[1] >> 28) & 0x03;
                out[18] = (in[1] >> 26) & 0x03;
                out[19] = (in[1] >> 24) & 0x03;
                out[20] = (in[1] >> 22) & 0x03;
                out[21] = (in[1] >> 20) & 0x03;
                out[22] = (in[1] >> 18) & 0x03;
                out[23] = (in[1] >> 16) & 0x03;
                out[24] = (in[1] >> 14) & 0x03;
                out[25] = (in[1] >> 12) & 0x03;
                out[26] = (in[1] >> 10) & 0x03;
                out[27] = (in[1] >> 8) & 0x03;
                out[28] = (in[1] >> 6) & 0x03;
                out[29] = (in[1] >> 4) & 0x03;
                out[30] = (in[1] >> 2) & 0x03;
                out[31] = in[1] & 0x03;
        }
}

void
__vseblocks_unpack3(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 3) {
                out[0] = in[0] >> 29;
                out[1] = (in[0] >> 26) & 0x07;
                out[2] = (in[0] >> 23) & 0x07;
                out[3] = (in[0] >> 20) & 0x07;
                out[4] = (in[0] >> 17) & 0x07;
                out[5] = (in[0] >> 14) & 0x07;
                out[6] = (in[0] >> 11) & 0x07;
                out[7] = (in[0] >> 8) & 0x07;
                out[8] = (in[0] >> 5) & 0x07;
                out[9] = (in[0] >> 2) & 0x07;
                out[10] = (in[0] << 1) & 0x07;
                out[10] |= in[1] >> 31;
                out[11] = (in[1] >> 28) & 0x07;
                out[12] = (in[1] >> 25) & 0x07;
                out[13] = (in[1] >> 22) & 0x07;
                out[14] = (in[1] >> 19) & 0x07;
                out[15] = (in[1] >> 16) & 0x07;
                out[16] = (in[1] >> 13) & 0x07;
                out[17] = (in[1] >> 10) & 0x07;
                out[18] = (in[1] >> 7) & 0x07;
                out[19] = (in[1] >> 4) & 0x07;
                out[20] = (in[1] >> 1) & 0x07;
                out[21] = (in[1] << 2) & 0x07;
                out[21] |= in[2] >> 30;
                out[22] = (in[2] >> 27) & 0x07;
                out[23] = (in[2] >> 24) & 0x07;
                out[24] = (in[2] >> 21) & 0x07;
                out[25] = (in[2] >> 18) & 0x07;
                out[26] = (in[2] >> 15) & 0x07;
                out[27] = (in[2] >> 12) & 0x07;
                out[28] = (in[2] >> 9) & 0x07;
                out[29] = (in[2] >> 6) & 0x07;
                out[30] = (in[2] >> 3) & 0x07;
                out[31] = in[2] & 0x07;
        }
}

void
__vseblocks_unpack4(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 4) {
                out[0] = in[0] >> 28;
                out[1] = (in[0] >> 24) & 0x0f;
                out[2] = (in[0] >> 20) & 0x0f;
                out[3] = (in[0] >> 16) & 0x0f;
                out[4] = (in[0] >> 12) & 0x0f;
                out[5] = (in[0] >> 8) & 0x0f;
                out[6] = (in[0] >> 4) & 0x0f;
                out[7] = in[0] & 0x0f;
                out[8] = in[1] >> 28;
                out[9] = (in[1] >> 24) & 0x0f;
                out[10] = (in[1] >> 20) & 0x0f;
                out[11] = (in[1] >> 16) & 0x0f;
                out[12] = (in[1] >> 12) & 0x0f;
                out[13] = (in[1] >> 8) & 0x0f;
                out[14] = (in[1] >> 4) & 0x0f;
                out[15] = in[1] & 0x0f;
                out[16] = in[2] >> 28;
                out[17] = (in[2] >> 24) & 0x0f;
                out[18] = (in[2] >> 20) & 0x0f;
                out[19] = (in[2] >> 16) & 0x0f;
                out[20] = (in[2] >> 12) & 0x0f;
                out[21] = (in[2] >> 8) & 0x0f;
                out[22] = (in[2] >> 4) & 0x0f;
                out[23] = in[2] & 0x0f;
                out[24] = in[3] >> 28;
                out[25] = (in[3] >> 24) & 0x0f;
                out[26] = (in[3] >> 20) & 0x0f;
                out[27] = (in[3] >> 16) & 0x0f;
                out[28] = (in[3] >> 12) & 0x0f;
                out[29] = (in[3] >> 8) & 0x0f;
                out[30] = (in[3] >> 4) & 0x0f;
                out[31] = in[3] & 0x0f;
        }
}

void
__vseblocks_unpack5(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 5) {
                out[0] = in[0] >> 27;
                out[1] = (in[0] >> 22) & 0x1f;
                out[2] = (in[0] >> 17) & 0x1f;
                out[3] = (in[0] >> 12) & 0x1f;
                out[4] = (in[0] >> 7) & 0x1f;
                out[5] = (in[0] >> 2) & 0x1f;
                out[6] = (in[0] << 3) & 0x1f;
                out[6] |= in[1] >> 29;
                out[7] = (in[1] >> 24) & 0x1f;
                out[8] = (in[1] >> 19) & 0x1f;
                out[9] = (in[1] >> 14) & 0x1f;
                out[10] = (in[1] >> 9) & 0x1f;
                out[11] = (in[1] >> 4) & 0x1f;
                out[12] = (in[1] << 1) & 0x1f;
                out[12] |= in[2] >> 0x1f;
                out[13] = (in[2] >> 26) & 0x1f;
                out[14] = (in[2] >> 21) & 0x1f;
                out[15] = (in[2] >> 16) & 0x1f;
                out[16] = (in[2] >> 11) & 0x1f;
                out[17] = (in[2] >> 6) & 0x1f;
                out[18] = (in[2] >> 1) & 0x1f;
                out[19] = (in[2] << 4) & 0x1f;
                out[19] |= in[3] >> 28;
                out[20] = (in[3] >> 23) & 0x1f;
                out[21] = (in[3] >> 18) & 0x1f;
                out[22] = (in[3] >> 13) & 0x1f;
                out[23] = (in[3] >> 8) & 0x1f;
                out[24] = (in[3] >> 3) & 0x1f;
                out[25] = (in[3] << 2) & 0x1f;
                out[25] |= in[4] >> 30;
                out[26] = (in[4] >> 25) & 0x1f;
                out[27] = (in[4] >> 20) & 0x1f;
                out[28] = (in[4] >> 15) & 0x1f;
                out[29] = (in[4] >> 10) & 0x1f;
                out[30] = (in[4] >> 5) & 0x1f;
                out[31] = in[4] & 0x1f;
        }
}

void
__vseblocks_unpack6(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 6) {
                out[0] = in[0] >> 26;
                out[1] = (in[0] >> 20) & 0x3f;
                out[2] = (in[0] >> 14) & 0x3f;
                out[3] = (in[0] >> 8) & 0x3f;
                out[4] = (in[0] >> 2) & 0x3f;
                out[5] = (in[0] << 4) & 0x3f;
                out[5] |= in[1] >> 28;
                out[6] = (in[1] >> 22) & 0x3f;
                out[7] = (in[1] >> 16) & 0x3f;
                out[8] = (in[1] >> 10) & 0x3f;
                out[9] = (in[1] >> 4) & 0x3f;
                out[10] = (in[1] << 2) & 0x3f;
                out[10] |= in[2] >> 30;
                out[11] = (in[2] >> 24) & 0x3f;
                out[12] = (in[2] >> 18) & 0x3f;
                out[13] = (in[2] >> 12) & 0x3f;
                out[14] = (in[2] >> 6) & 0x3f;
                out[15] = in[2] & 0x3f;
                out[16] = in[3] >> 26;
                out[17] = (in[3] >> 20) & 0x3f;
                out[18] = (in[3] >> 14) & 0x3f;
                out[19] = (in[3] >> 8) & 0x3f;
                out[20] = (in[3] >> 2) & 0x3f;
                out[21] = (in[3] << 4) & 0x3f;
                out[21] |= in[4] >> 28;
                out[22] = (in[4] >> 22) & 0x3f;
                out[23] = (in[4] >> 16) & 0x3f;
                out[24] = (in[4] >> 10) & 0x3f;
                out[25] = (in[4] >> 4) & 0x3f;
                out[26] = (in[4] << 2) & 0x3f;
                out[26] |= in[5] >> 30;
                out[27] = (in[5] >> 24) & 0x3f;
                out[28] = (in[5] >> 18) & 0x3f;
                out[29] = (in[5] >> 12) & 0x3f;
                out[30] = (in[5] >> 6) & 0x3f;
                out[31] = in[5] & 0x3f;
        }
}

void
__vseblocks_unpack7(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 7) {
                out[0] = in[0] >> 25;
                out[1] = (in[0] >> 18) & 0x7f;
                out[2] = (in[0] >> 11) & 0x7f;
                out[3] = (in[0] >> 4) & 0x7f;
                out[4] = (in[0] << 3) & 0x7f;
                out[4] |= in[1] >> 29;
                out[5] = (in[1] >> 22) & 0x7f;
                out[6] = (in[1] >> 15) & 0x7f;
                out[7] = (in[1] >> 8) & 0x7f;
                out[8] = (in[1] >> 1) & 0x7f;
                out[9] = (in[1] << 6) & 0x7f;
                out[9] |= in[2] >> 26;
                out[10] = (in[2] >> 19) & 0x7f;
                out[11] = (in[2] >> 12) & 0x7f;
                out[12] = (in[2] >> 5) & 0x7f;
                out[13] = (in[2] << 2) & 0x7f;
                out[13] |= in[3] >> 30;
                out[14] = (in[3] >> 23) & 0x7f;
                out[15] = (in[3] >> 16) & 0x7f;
                out[16] = (in[3] >> 9) & 0x7f;
                out[17] = (in[3] >> 2) & 0x7f;
                out[18] = (in[3] << 5) & 0x7f;
                out[18] |= in[4] >> 27;
                out[19] = (in[4] >> 20) & 0x7f;
                out[20] = (in[4] >> 13) & 0x7f;
                out[21] = (in[4] >> 6) & 0x7f;
                out[22] = (in[4] << 1) & 0x7f;
                out[22] |= in[5] >> 31;
                out[23] = (in[5] >> 24) & 0x7f;
                out[24] = (in[5] >> 17) & 0x7f;
                out[25] = (in[5] >> 10) & 0x7f;
                out[26] = (in[5] >> 3) & 0x7f;
                out[27] = (in[5] << 4) & 0x7f;
                out[27] |= in[6] >> 28;
                out[28] = (in[6] >> 21) & 0x7f;
                out[29] = (in[6] >> 14) & 0x7f;
                out[30] = (in[6] >> 7) & 0x7f;
                out[31] = in[6] & 0x7f;
        }
}

void __vseblocks_unpack8(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 8) {
                out[0] = in[0] >> 24;
                out[1] = (in[0] >> 16) & 0xff;
                out[2] = (in[0] >> 8) & 0xff;
                out[3] = in[0] & 0xff;
                out[4] = in[1] >> 24;
                out[5] = (in[1] >> 16) & 0xff;
                out[6] = (in[1] >> 8) & 0xff;
                out[7] = in[1] & 0xff;
                out[8] = in[2] >> 24;
                out[9] = (in[2] >> 16) & 0xff;
                out[10] = (in[2] >> 8) & 0xff;
                out[11] = in[2] & 0xff;
                out[12] = in[3] >> 24;
                out[13] = (in[3] >> 16) & 0xff;
                out[14] = (in[3] >> 8) & 0xff;
                out[15] = in[3] & 0xff;
                out[16] = in[4] >> 24;
                out[17] = (in[4] >> 16) & 0xff;
                out[18] = (in[4] >> 8) & 0xff;
                out[19] = in[4] & 0xff;
                out[20] = in[5] >> 24;
                out[21] = (in[5] >> 16) & 0xff;
                out[22] = (in[5] >> 8) & 0xff;
                out[23] = in[5] & 0xff;
                out[24] = in[6] >> 24;
                out[25] = (in[6] >> 16) & 0xff;
                out[26] = (in[6] >> 8) & 0xff;
                out[27] = in[6] & 0xff;
                out[28] = in[7] >> 24;
                out[29] = (in[7] >> 16) & 0xff;
                out[30] = (in[7] >> 8) & 0xff;
                out[31] = in[7] & 0xff;
        }
}

void
__vseblocks_unpack9(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 9) {
                out[0] = in[0] >> 23;
                out[1] = (in[0] >> 14) & 0x01ff;
                out[2] = (in[0] >> 5) & 0x01ff;
                out[3] = (in[0] << 4) & 0x01ff;
                out[3] |= in[1] >> 28;
                out[4] = (in[1] >> 19) & 0x01ff;
                out[5] = (in[1] >> 10) & 0x01ff;
                out[6] = (in[1] >> 1) & 0x01ff;
                out[7] = (in[1] << 8) & 0x01ff;
                out[7] |= in[2] >> 24;
                out[8] = (in[2] >> 15) & 0x01ff;
                out[9] = (in[2] >> 6) & 0x01ff;
                out[10] = (in[2] << 3) & 0x01ff;
                out[10] |= in[3] >> 29;
                out[11] = (in[3] >> 20) & 0x01ff;
                out[12] = (in[3] >> 11) & 0x01ff;
                out[13] = (in[3] >> 2) & 0x01ff;
                out[14] = (in[3] << 7) & 0x01ff;
                out[14] |= in[4] >> 25;
                out[15] = (in[4] >> 16) & 0x01ff;
                out[16] = (in[4] >> 7) & 0x01ff;
                out[17] = (in[4] << 2) & 0x01ff;
                out[17] |= in[5] >> 30;
                out[18] = (in[5] >> 21) & 0x01ff;
                out[19] = (in[5] >> 12) & 0x01ff;
                out[20] = (in[5] >> 3) & 0x01ff;
                out[21] = (in[5] << 6) & 0x01ff;
                out[21] |= in[6] >> 26;
                out[22] = (in[6] >> 17) & 0x01ff;
                out[23] = (in[6] >> 8) & 0x01ff;
                out[24] = (in[6] << 1) & 0x01ff;
                out[24] |= in[7] >> 31;
                out[25] = (in[7] >> 22) & 0x01ff;
                out[26] = (in[7] >> 13) & 0x01ff;
                out[27] = (in[7] >> 4) & 0x01ff;
                out[28] = (in[7] << 5) & 0x01ff;
                out[28] |= in[8] >> 27;
                out[29] = (in[8] >> 18) & 0x01ff;
                out[30] = (in[8] >> 9) & 0x01ff;
                out[31] = in[8] & 0x01ff;
        }
}

void
__vseblocks_unpack10(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 10) {
                out[0] = in[0] >> 22;
                out[1] = (in[0] >> 12) & 0x03ff;
                out[2] = (in[0] >> 2) & 0x03ff;
                out[3] = (in[0] << 8) & 0x03ff;
                out[3] |= in[1] >> 24;
                out[4] = (in[1] >> 14) & 0x03ff;
                out[5] = (in[1] >> 4) & 0x03ff;
                out[6] = (in[1] << 6) & 0x03ff;
                out[6] |= in[2] >> 26;
                out[7] = (in[2] >> 16) & 0x03ff;
                out[8] = (in[2] >> 6) & 0x03ff;
                out[9] = (in[2] << 4) & 0x03ff;
                out[9] |= in[3] >> 28;
                out[10] = (in[3] >> 18) & 0x03ff;
                out[11] = (in[3] >> 8) & 0x03ff;
                out[12] = (in[3] << 2) & 0x03ff;
                out[12] |= in[4] >> 30;
                out[13] = (in[4] >> 20) & 0x03ff;
                out[14] = (in[4] >> 10) & 0x03ff;
                out[15] = in[4] & 0x03ff;
                out[16] = in[5] >> 22;
                out[17] = (in[5] >> 12) & 0x03ff;
                out[18] = (in[5] >> 2) & 0x03ff;
                out[19] = (in[5] << 8) & 0x03ff;
                out[19] |= in[6] >> 24;
                out[20] = (in[6] >> 14) & 0x03ff;
                out[21] = (in[6] >> 4) & 0x03ff;
                out[22] = (in[6] << 6) & 0x03ff;
                out[22] |= in[7] >> 26;
                out[23] = (in[7] >> 16) & 0x03ff;
                out[24] = (in[7] >> 6) & 0x03ff;
                out[25] = (in[7] << 4) & 0x03ff;
                out[25] |= in[8] >> 28;
                out[26] = (in[8] >> 18) & 0x03ff;
                out[27] = (in[8] >> 8) & 0x03ff;
                out[28] = (in[8] << 2) & 0x03ff;
                out[28] |= in[9] >> 30;
                out[29] = (in[9] >> 20) & 0x03ff;
                out[30] = (in[9] >> 10) & 0x03ff;
                out[31] = in[9] & 0x03ff;
        }
}

void
__vseblocks_unpack11(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 11) {
                out[0] = in[0] >> 21;
                out[1] = (in[0] >> 10) & 0x07ff;
                out[2] = (in[0] << 1) & 0x07ff;
                out[2] |= in[1] >> 31;
                out[3] = (in[1] >> 20) & 0x07ff;
                out[4] = (in[1] >> 9) & 0x07ff;
                out[5] = (in[1] << 2) & 0x07ff;
                out[5] |= in[2] >> 30;
                out[6] = (in[2] >> 19) & 0x07ff;
                out[7] = (in[2] >> 8) & 0x07ff;
                out[8] = (in[2] << 3) & 0x07ff;
                out[8] |= in[3] >> 29;
                out[9] = (in[3] >> 18) & 0x07ff;
                out[10] = (in[3] >> 7) & 0x07ff;
                out[11] = (in[3] << 4) & 0x07ff;
                out[11] |= in[4] >> 28;
                out[12] = (in[4] >> 17) & 0x07ff;
                out[13] = (in[4] >> 6) & 0x07ff;
                out[14] = (in[4] << 5) & 0x07ff;
                out[14] |= in[5] >> 27;
                out[15] = (in[5] >> 16) & 0x07ff;
                out[16] = (in[5] >> 5) & 0x07ff;
                out[17] = (in[5] << 6) & 0x07ff;
                out[17] |= in[6] >> 26;
                out[18] = (in[6] >> 15) & 0x07ff;
                out[19] = (in[6] >> 4) & 0x07ff;
                out[20] = (in[6] << 7) & 0x07ff;
                out[20] |= in[7] >> 25;
                out[21] = (in[7] >> 14) & 0x07ff;
                out[22] = (in[7] >> 3) & 0x07ff;
                out[23] = (in[7] << 8) & 0x07ff;
                out[23] |= in[8] >> 24;
                out[24] = (in[8] >> 13) & 0x07ff;
                out[25] = (in[8] >> 2) & 0x07ff;
                out[26] = (in[8] << 9) & 0x07ff;
                out[26] |= in[9] >> 23;
                out[27] = (in[9] >> 12) & 0x07ff;
                out[28] = (in[9] >> 1) & 0x07ff;
                out[29] = (in[9] << 10) & 0x07ff;
                out[29] |= in[10] >> 22;
                out[30] = (in[10] >> 11) & 0x07ff;
                out[31] = in[10] & 0x07ff;
        }
}

void
__vseblocks_unpack12(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 12) {
                out[0] = in[0] >> 20;
                out[1] = (in[0] >> 8) & 0x0fff;
                out[2] = (in[0] << 4) & 0x0fff;
                out[2] |= in[1] >> 28;
                out[3] = (in[1] >> 16) & 0x0fff;
                out[4] = (in[1] >> 4) & 0x0fff;
                out[5] = (in[1] << 8) & 0x0fff;
                out[5] |= in[2] >> 24;
                out[6] = (in[2] >> 12) & 0x0fff;
                out[7] = in[2] & 0x0fff;
                out[8] = in[3] >> 20;
                out[9] = (in[3] >> 8) & 0x0fff;
                out[10] = (in[3] << 4) & 0x0fff;
                out[10] |= in[4] >> 28;
                out[11] = (in[4] >> 16) & 0x0fff;
                out[12] = (in[4] >> 4) & 0x0fff;
                out[13] = (in[4] << 8) & 0x0fff;
                out[13] |= in[5] >> 24;
                out[14] = (in[5] >> 12) & 0x0fff;
                out[15] = in[5] & 0x0fff;
                out[16] = in[6] >> 20;
                out[17] = (in[6] >> 8) & 0x0fff;
                out[18] = (in[6] << 4) & 0x0fff;
                out[18] |= in[7] >> 28;
                out[19] = (in[7] >> 16) & 0x0fff;
                out[20] = (in[7] >> 4) & 0x0fff;
                out[21] = (in[7] << 8) & 0x0fff;
                out[21] |= in[8] >> 24;
                out[22] = (in[8] >> 12) & 0x0fff;
                out[23] = in[8] & 0x0fff;
                out[24] = in[9] >> 20;
                out[25] = (in[9] >> 8) & 0x0fff;
                out[26] = (in[9] << 4) & 0x0fff;
                out[26] |= in[10] >> 28;
                out[27] = (in[10] >> 16) & 0x0fff;
                out[28] = (in[10] >> 4) & 0x0fff;
                out[29] = (in[10] << 8) & 0x0fff;
                out[29] |= in[11] >> 24;
                out[30] = (in[11] >> 12) & 0x0fff;
                out[31] = in[11] & 0x0fff;
        }
}

void
__vseblocks_unpack16(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 16) {
                out[0] = in[0] >> 16;
                out[1] = in[0] & 0xffff;
                out[2] = in[1] >> 16;
                out[3] = in[1] & 0xffff;
                out[4] = in[2] >> 16;
                out[5] = in[2] & 0xffff;
                out[6] = in[3] >> 16;
                out[7] = in[3] & 0xffff;
                out[8] = in[4] >> 16;
                out[9] = in[4] & 0xffff;
                out[10] = in[5] >> 16;
                out[11] = in[5] & 0xffff;
                out[12] = in[6] >> 16;
                out[13] = in[6] & 0xffff;
                out[14] = in[7] >> 16;
                out[15] = in[7] & 0xffff;
                out[16] = in[8] >> 16;
                out[17] = in[8] & 0xffff;
                out[18] = in[9] >> 16;
                out[19] = in[9] & 0xffff;
                out[20] = in[10] >> 16;
                out[21] = in[10] & 0xffff;
                out[22] = in[11] >> 16;
                out[23] = in[11] & 0xffff;
                out[24] = in[12] >> 16;
                out[25] = in[12] & 0xffff;
                out[26] = in[13] >> 16;
                out[27] = in[13] & 0xffff;
                out[28] = in[14] >> 16;
                out[29] = in[14] & 0xffff;
                out[30] = in[15] >> 16;
                out[31] = in[15] & 0xffff;
        }
}

void
__vseblocks_unpack20(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs; i += 32, out += 32, in += 20) {
                out[0] = in[0] >> 12;
                out[1] = (in[0] << 8) & 0x0fffff;
                out[1] |= in[1] >> 24;
                out[2] = (in[1] >> 4) & 0x0fffff;
                out[3] = (in[1] << 16) & 0x0fffff;
                out[3] |= in[2] >> 16;
                out[4] = (in[2] << 4) & 0x0fffff;
                out[4] |= in[3] >> 28;
                out[5] = (in[3] >> 8) & 0x0fffff;
                out[6] = (in[3] << 12) & 0x0fffff;
                out[6] |= in[4] >> 20;
                out[7] = in[4] & 0x0fffff;
                out[8] = in[5] >> 12;
                out[9] = (in[5] << 8) & 0x0fffff;
                out[9] |= in[6] >> 24;
                out[10] = (in[6] >> 4) & 0x0fffff;
                out[11] = (in[6] << 16) & 0x0fffff;
                out[11] |= in[7] >> 16;
                out[12] = (in[7] << 4) & 0x0fffff;
                out[12] |= in[8] >> 28;
                out[13] = (in[8] >> 8) & 0x0fffff;
                out[14] = (in[8] << 12) & 0x0fffff;
                out[14] |= in[9] >> 20;
                out[15] = in[9] & 0x0fffff;
                out[16] = in[10] >> 12;
                out[17] = (in[10] << 8) & 0x0fffff;
                out[17] |= in[11] >> 24;
                out[18] = (in[11] >> 4) & 0x0fffff;
                out[19] = (in[11] << 16) & 0x0fffff;
                out[19] |= in[12] >> 16;
                out[20] = (in[12] << 4) & 0x0fffff;
                out[20] |= in[13] >> 28;
                out[21] = (in[13] >> 8) & 0x0fffff;
                out[22] = (in[13] << 12) & 0x0fffff;
                out[22] |= in[14] >> 20;
                out[23] = in[14] & 0x0fffff;
                out[24] = in[15] >> 12;
                out[25] = (in[15] << 8) & 0x0fffff;
                out[25] |= in[16] >> 24;
                out[26] = (in[16] >> 4) & 0x0fffff;
                out[27] = (in[16] << 16) & 0x0fffff;
                out[27] |= in[17] >> 16;
                out[28] = (in[17] << 4) & 0x0fffff;
                out[28] |= in[18] >> 28;
                out[29] = (in[18] >> 8) & 0x0fffff;
                out[30] = (in[18] << 12) & 0x0fffff;
                out[30] |= in[19] >> 20;
                out[31] = in[19] & 0x0fffff;
        }
}

void
__vseblocks_unpack32(uint32_t *out, uint32_t *in, uint32_t bs)
{
        for (uint32_t i = 0; i < bs;
                        i += 16, out += 16, in += 16) {
                __vseblocks_copy16(in, out);
        }
}

