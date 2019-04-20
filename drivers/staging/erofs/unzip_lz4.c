// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/erofs/unzip_lz4.c
 *
 * Copyright (c) 2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "internal.h"
#include <asm/unaligned.h>
#include "lz4defs.h"

/*
 * no public solution to solve our requirement yet.
 * see: <required buffer size for LZ4_decompress_safe_partial>
 *      https://groups.google.com/forum/#!topic/lz4c/_3kkz5N6n00
 */
static FORCE_INLINE int customized_lz4_decompress_safe_partial(
	const void * const source,
	void * const dest,
	int inputSize,
	int outputSize)
{
	/* Local Variables */
	const BYTE *ip = (const BYTE *) source;
	const BYTE * const iend = ip + inputSize;

	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + outputSize;
	BYTE *cpy;

	static const unsigned int dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };
	static const int dec64table[] = { 0, 0, 0, -1, 0, 1, 2, 3 };

	/* Empty output buffer */
	if (unlikely(outputSize == 0))
		return ((inputSize == 1) && (*ip == 0)) ? 0 : -1;

	/* Main Loop : decode sequences */
	while (1) {
		size_t length;
		const BYTE *match;
		size_t offset;

		/* get literal length */
		unsigned int const token = *ip++;

		length = token>>ML_BITS;

		if (length == RUN_MASK) {
			unsigned int s;

			do {
				s = *ip++;
				length += s;
			} while ((ip < iend - RUN_MASK) & (s == 255));

			if (unlikely((size_t)(op + length) < (size_t)(op))) {
				/* overflow detection */
				goto _output_error;
			}
			if (unlikely((size_t)(ip + length) < (size_t)(ip))) {
				/* overflow detection */
				goto _output_error;
			}
		}

		/* copy literals */
		cpy = op + length;
		if ((cpy > oend - WILDCOPYLENGTH) ||
			(ip + length > iend - (2 + 1 + LASTLITERALS))) {
			if (cpy > oend) {
				memcpy(op, ip, length = oend - op);
				op += length;
				break;
			}

			if (unlikely(ip + length > iend)) {
				/*
				 * Error :
				 * read attempt beyond
				 * end of input buffer
				 */
				goto _output_error;
			}

			memcpy(op, ip, length);
			ip += length;
			op += length;

			if (ip > iend - 2)
				break;
			/* Necessarily EOF, due to parsing restrictions */
			/* break; */
		} else {
			LZ4_wildCopy(op, ip, cpy);
			ip += length;
			op = cpy;
		}

		/* get offset */
		offset = LZ4_readLE16(ip);
		ip += 2;
		match = op - offset;

		if (unlikely(match < (const BYTE *)dest)) {
			/* Error : offset outside buffers */
			goto _output_error;
		}

		/* get matchlength */
		length = token & ML_MASK;
		if (length == ML_MASK) {
			unsigned int s;

			do {
				s = *ip++;

				if (ip > iend - LASTLITERALS)
					goto _output_error;

				length += s;
			} while (s == 255);

			if (unlikely((size_t)(op + length) < (size_t)op)) {
				/* overflow detection */
				goto _output_error;
			}
		}

		length += MINMATCH;

		/* copy match within block */
		cpy = op + length;

		if (unlikely(cpy >= oend - WILDCOPYLENGTH)) {
			if (cpy >= oend) {
				while (op < oend)
					*op++ = *match++;
				break;
			}
			goto __match;
		}

		/* costs ~1%; silence an msan warning when offset == 0 */
		LZ4_write32(op, (U32)offset);

		if (unlikely(offset < 8)) {
			const int dec64 = dec64table[offset];

			op[0] = match[0];
			op[1] = match[1];
			op[2] = match[2];
			op[3] = match[3];
			match += dec32table[offset];
			memcpy(op + 4, match, 4);
			match -= dec64;
		} else {
			LZ4_copy8(op, match);
			match += 8;
		}

		op += 8;

		if (unlikely(cpy > oend - 12)) {
			BYTE * const oCopyLimit = oend - (WILDCOPYLENGTH - 1);

			if (op < oCopyLimit) {
				LZ4_wildCopy(op, match, oCopyLimit);
				match += oCopyLimit - op;
				op = oCopyLimit;
			}
__match:
			while (op < cpy)
				*op++ = *match++;
		} else {
			LZ4_copy8(op, match);

			if (length > 16)
				LZ4_wildCopy(op + 8, match + 8, cpy);
		}

		op = cpy; /* correction */
	}
	DBG_BUGON((void *)ip - source > inputSize);
	DBG_BUGON((void *)op - dest > outputSize);

	/* Nb of output bytes decoded */
	return (int) ((void *)op - dest);

	/* Overflow error detected */
_output_error:
	return -ERANGE;
}

int erofs_unzip_lz4(void *in, void *out, size_t inlen, size_t outlen)
{
	int ret = customized_lz4_decompress_safe_partial(in,
		out, inlen, outlen);

	if (ret >= 0)
		return ret;

	/*
	 * LZ4_decompress_safe will return an error code
	 * (< 0) if decompression failed
	 */
	errln("%s, failed to decompress, in[%p, %lu] outlen[%p, %lu]",
	      __func__, in, inlen, out, outlen);
	WARN_ON(1);
	print_hex_dump(KERN_DEBUG, "raw data [in]: ", DUMP_PREFIX_OFFSET,
		16, 1, in, inlen, true);
	print_hex_dump(KERN_DEBUG, "raw data [out]: ", DUMP_PREFIX_OFFSET,
		16, 1, out, outlen, true);
	return -EIO;
}

