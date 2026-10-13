#include "bs/core/png.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1])
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static bool write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put_be32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, hdr + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put_be32(tail, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(tail, 1, 4, f) == 4;
}

/* channels: 1 = indexed (with palette), 3 = RGB. */
static bs_result write_png(const char *path, uint32_t width, uint32_t height, int channels, const uint8_t *pixels,
                           const uint8_t *palette, int transparent_index)
{
    crc_init();
    if (width == 0 || height == 0)
        return BS_ERR_ARGS;

    /* Scanlines: filter byte 0 + row. */
    size_t stride = (size_t)width * (size_t)channels;
    size_t row = stride + 1;
    size_t raw_len = row * height;
    /* zlib: 2-byte header, stored blocks of up to 65535 bytes (5-byte header each), adler32. */
    size_t blocks = raw_len / 65535 + 1;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len);
    uint8_t *z = malloc(z_len);
    if (!raw || !z) {
        free(raw);
        free(z);
        return BS_ERR_NOMEM;
    }
    for (uint32_t y = 0; y < height; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, pixels + (size_t)y * stride, stride);
    }

    size_t zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0;;) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        bool final = off + n >= raw_len;
        z[zp++] = final ? 1 : 0;
        z[zp++] = (uint8_t)n;
        z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)~n;
        z[zp++] = (uint8_t)(~n >> 8);
        memcpy(z + zp, raw + off, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        zp += n;
        off += n;
        if (final)
            break;
    }
    put_be32(z + zp, (b << 16) | a);
    zp += 4;

    bs_result res = BS_ERR_IO;
    FILE *f = fopen(path, "wb");
    if (f) {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        uint8_t ihdr[13];
        put_be32(ihdr, width);
        put_be32(ihdr + 4, height);
        ihdr[8] = 8;  /* bit depth */
        ihdr[9] = channels == 1 ? 3 : 2;  /* indexed or RGB */
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        uint8_t trns[256];
        memset(trns, 255, sizeof trns);
        bool ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13);
        if (ok && palette)
            ok = write_chunk(f, "PLTE", palette, 768);
        if (ok && palette && transparent_index >= 0 && transparent_index < 256) {
            trns[transparent_index] = 0;
            ok = write_chunk(f, "tRNS", trns, (size_t)transparent_index + 1);
        }
        ok = ok && write_chunk(f, "IDAT", z, zp) && write_chunk(f, "IEND", NULL, 0);
        if (fclose(f) != 0)
            ok = false;
        res = ok ? BS_OK : BS_ERR_IO;
    }
    free(raw);
    free(z);
    return res;
}

bs_result bs_png_write_indexed(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels,
                               const uint8_t palette[768], int transparent_index)
{
    return write_png(path, width, height, 1, pixels, palette, transparent_index);
}

bs_result bs_png_write_rgb(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels)
{
    return write_png(path, width, height, 3, pixels, NULL, -1);
}
