/*
 * copy - Copy files and directories
 *
 * Copyright (C) 2014 Nathan Forbes
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The methods of generating an MD5 message digest below were derived from
 * the RSA Data Security, Inc. MD5 Message-Digest Algorithm.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "copy-checksum.h"
#include "copy-utils.h"

#define S11  7
#define S12 12
#define S13 17
#define S14 22
#define S21  5
#define S22  9
#define S23 14
#define S24 20
#define S31  4
#define S32 11
#define S33 16
#define S34 23
#define S41  6
#define S42 10
#define S43 15
#define S44 21

#define F(x, y, z)        (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z)        (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z)        ((x) ^ (y) ^ (z))
#define I(x, y, z)        ((y) ^ ((x) | (~z)))
#define rotate_left(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac) \
  do \
  { \
    (a) += F ((b), (c), (d)) + (x) + (ac); \
    (a) = rotate_left ((a), (s)); \
    (a) += (b); \
  } while (0)

#define GG(a, b, c, d, x, s, ac) \
  do \
  { \
    (a) += G ((b), (c), (d)) + (x) + (ac); \
    (a) = rotate_left ((a), (s)); \
    (a) += (b); \
  } while (0)

#define HH(a, b, c, d, x, s, ac) \
  do \
  { \
    (a) += H ((b), (c), (d)) + (x) + (ac); \
    (a) = rotate_left ((a), (s)); \
    (a) += (b); \
  } while (0)

#define II(a, b, c, d, x, s, ac) \
  do \
  { \
    (a) += I ((b), (c), (d)) + (x) + (ac); \
    (a) = rotate_left ((a), (s)); \
    (a) += (b); \
  } while (0)

struct md5_ctx
{
  unsigned int state[4];
  unsigned int count[2];
  unsigned char buffer[64];
};

static unsigned char padding[64] =
{
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void
md5_encode (unsigned char *o, unsigned int *i, unsigned int n)
{
  unsigned int x;
  unsigned int y;

  for (x = 0, y = 0; y < n; x++, y += 4)
  {
    o[y] = (unsigned char) (i[x] & 0xff);
    o[y + 1] = (unsigned char) ((i[x] >> 8) & 0xff);
    o[y + 2] = (unsigned char) ((i[x] >> 16) & 0xff);
    o[y + 3] = (unsigned char) ((i[x] >> 24) & 0xff);
  }
}

static void
md5_decode (unsigned int *o, unsigned char *i, unsigned int n)
{
  unsigned int x;
  unsigned int y;

  for (x = 0, y = 0; y < n; x++, y += 4)
    o[x] = ((unsigned int) i[y]) |
           (((unsigned int) i[y + 1]) << 8) |
           (((unsigned int) i[y + 2]) << 16) |
           (((unsigned int) i[y + 3]) << 24);
}

static void
md5_transform (unsigned int state[4], unsigned char block[64])
{
  unsigned int a;
  unsigned int b;
  unsigned int c;
  unsigned int d;
  unsigned int x[16];

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];

  md5_decode (x, block, 64);

  FF (a, b, c, d, x[0], S11, 0xd76aa478);
  FF (d, a, b, c, x[1], S12, 0xe8c7b756);
  FF (c, d, a, b, x[2], S13, 0x242070db);
  FF (b, c, d, a, x[3], S14, 0xc1bdceee);
  FF (a, b, c, d, x[4], S11, 0xf57c0faf);
  FF (d, a, b, c, x[5], S12, 0x4787c62a);
  FF (c, d, a, b, x[6], S13, 0xa8304613);
  FF (b, c, d, a, x[7], S14, 0xfd469501);
  FF (a, b, c, d, x[8], S11, 0x698098d8);
  FF (d, a, b, c, x[9], S12, 0x8b44f7af);
  FF (c, d, a, b, x[10], S13, 0xffff5bb1);
  FF (b, c, d, a, x[11], S14, 0x895cd7be);
  FF (a, b, c, d, x[12], S11, 0x6b901122);
  FF (d, a, b, c, x[13], S12, 0xfd987193);
  FF (c, d, a, b, x[14], S13, 0xa679438e);
  FF (b, c, d, a, x[15], S14, 0x49b40821);

  GG (a, b, c, d, x[1], S21, 0xf61e2562);
  GG (d, a, b, c, x[6], S22, 0xc040b340);
  GG (c, d, a, b, x[11], S23, 0x265e5a51);
  GG (b, c, d, a, x[0], S24, 0xe9b6c7aa);
  GG (a, b, c, d, x[5], S21, 0xd62f105d);
  GG (d, a, b, c, x[10], S22,  0x2441453);
  GG (c, d, a, b, x[15], S23, 0xd8a1e681);
  GG (b, c, d, a, x[4], S24, 0xe7d3fbc8);
  GG (a, b, c, d, x[9], S21, 0x21e1cde6);
  GG (d, a, b, c, x[14], S22, 0xc33707d6);
  GG (c, d, a, b, x[3], S23, 0xf4d50d87);
  GG (b, c, d, a, x[8], S24, 0x455a14ed);
  GG (a, b, c, d, x[13], S21, 0xa9e3e905);
  GG (d, a, b, c, x[2], S22, 0xfcefa3f8);
  GG (c, d, a, b, x[7], S23, 0x676f02d9);
  GG (b, c, d, a, x[12], S24, 0x8d2a4c8a);

  HH (a, b, c, d, x[5], S31, 0xfffa3942);
  HH (d, a, b, c, x[8], S32, 0x8771f681);
  HH (c, d, a, b, x[11], S33, 0x6d9d6122);
  HH (b, c, d, a, x[14], S34, 0xfde5380c);
  HH (a, b, c, d, x[1], S31, 0xa4beea44);
  HH (d, a, b, c, x[4], S32, 0x4bdecfa9);
  HH (c, d, a, b, x[7], S33, 0xf6bb4b60);
  HH (b, c, d, a, x[10], S34, 0xbebfbc70);
  HH (a, b, c, d, x[13], S31, 0x289b7ec6);
  HH (d, a, b, c, x[0], S32, 0xeaa127fa);
  HH (c, d, a, b, x[3], S33, 0xd4ef3085);
  HH (b, c, d, a, x[6], S34,  0x4881d05);
  HH (a, b, c, d, x[9], S31, 0xd9d4d039);
  HH (d, a, b, c, x[12], S32, 0xe6db99e5);
  HH (c, d, a, b, x[15], S33, 0x1fa27cf8);
  HH (b, c, d, a, x[2], S34, 0xc4ac5665);

  II (a, b, c, d, x[0], S41, 0xf4292244);
  II (d, a, b, c, x[7], S42, 0x432aff97);
  II (c, d, a, b, x[14], S43, 0xab9423a7);
  II (b, c, d, a, x[5], S44, 0xfc93a039);
  II (a, b, c, d, x[12], S41, 0x655b59c3);
  II (d, a, b, c, x[3], S42, 0x8f0ccc92);
  II (c, d, a, b, x[10], S43, 0xffeff47d);
  II (b, c, d, a, x[1], S44, 0x85845dd1);
  II (a, b, c, d, x[8], S41, 0x6fa87e4f);
  II (d, a, b, c, x[15], S42, 0xfe2ce6e0);
  II (c, d, a, b, x[6], S43, 0xa3014314);
  II (b, c, d, a, x[13], S44, 0x4e0811a1);
  II (a, b, c, d, x[4], S41, 0xf7537e82);
  II (d, a, b, c, x[11], S42, 0xbd3af235);
  II (c, d, a, b, x[2], S43, 0x2ad7d2bb);
  II (b, c, d, a, x[9], S44, 0xeb86d391);

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  memset ((unsigned char *) x, 0, sizeof (x));
}

static void
md5_init (struct md5_ctx *ctx)
{
  ctx->count[0] = 0;
  ctx->count[1] = 0;

  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xefcdab89;
  ctx->state[2] = 0x98badcfe;
  ctx->state[3] = 0x10325476;
}

static void
md5_update (struct md5_ctx *ctx, unsigned char *input, unsigned int n)
{
  unsigned int i;
  unsigned int index;
  unsigned int n_part;

  index = ((ctx->count[0] >> 3) & 0x3F);
  if ((ctx->count[0] += (n << 3)) < (n << 3))
    ctx->count[1]++;

  ctx->count[1] += (n >> 29);
  n_part = 64 - index;

  if (n >= n_part)
  {
    memcpy (&ctx->buffer[index], input, n_part);
    md5_transform (ctx->state, ctx->buffer);
    for (i = n_part; i + 63 < n; i += 64)
      md5_transform (ctx->state, &input[i]);
    index = 0;
  }
  else
    i = 0;
  memcpy (&ctx->buffer[index], &input[i], n - i);
}

static void
md5_update_from_file (struct md5_ctx *ctx, FILE *fp)
{
  unsigned int n;
  unsigned char buffer[1024];

  for (;;)
  {
    n = fread (buffer, 1, 1024, fp);
    if (!n)
      break;
    md5_update (ctx, buffer, n);
  }
}

static void
md5_final (struct md5_ctx *ctx, unsigned char digest[MD5_DIGEST_SIZE])
{
  unsigned int index;
  unsigned int n_pad;
  unsigned char bits[8];

  md5_encode (bits, ctx->count, 8);
  index = ((ctx->count[0] >> 3) & 0x3f);
  n_pad = (index < 56) ? (56 - index) : (120 - index);

  md5_update (ctx, padding, n_pad);
  md5_update (ctx, bits, 8);

  md5_encode (digest, ctx->state, MD5_DIGEST_SIZE);
  memset ((unsigned char *) ctx, 0, sizeof (*ctx));
}

static void
md5_from_digest (char buffer[CHECKSUM_BUFMAX],
                 unsigned char digest[MD5_DIGEST_SIZE])
{
  unsigned int i;

  for (i = 0; i < MD5_DIGEST_SIZE; i++)
    sprintf (buffer + i * 2, "%02x", digest[i]);
  buffer[CHECKSUM_BUFMAX - 1] = '\0';
}

void
get_checksum (char *buffer, const char *path)
{
  struct md5_ctx ctx;
  FILE *fp;
  unsigned char digest[MD5_DIGEST_SIZE];

  fp = fopen (path, "rb");
  if (!fp)
    die (errno, "failed to open `%s' to generate MD5 checksum", path);
  memset (&ctx, 0, sizeof (struct md5_ctx));
  md5_init (&ctx);
  md5_update_from_file (&ctx, fp);
  md5_final (&ctx, digest);
  md5_from_digest (buffer, digest);
  fclose (fp);
}

