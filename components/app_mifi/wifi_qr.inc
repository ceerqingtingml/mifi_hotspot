/* Compact ISO/IEC 18004 QR Code encoder (Byte mode, ECC Level L, Version 3/4)
 * for live Wi-Fi connection payloads ("WIFI:T:...;S:...;P:...;;").
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define QR_MAX_SIZE 33

typedef struct {
    uint8_t size;
    uint8_t modules[QR_MAX_SIZE][QR_MAX_SIZE];
    uint8_t is_func[QR_MAX_SIZE][QR_MAX_SIZE];
} wifi_qr_t;

static uint8_t gf_mul(uint8_t x, uint8_t y)
{
    uint8_t z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (uint8_t)((z << 1) ^ ((z >> 7) * 0x1D));
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}

static void rs_compute_divisor(int degree, uint8_t *coeff)
{
    memset(coeff, 0, (size_t)degree);
    coeff[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            coeff[j] = gf_mul(coeff[j], root);
            if (j + 1 < degree) {
                coeff[j] ^= coeff[j + 1];
            }
        }
        root = gf_mul(root, 0x02);
    }
}

static void rs_get_remainder(const uint8_t *data, int data_len,
                             const uint8_t *coeff, int degree, uint8_t *rem)
{
    memset(rem, 0, (size_t)degree);
    for (int i = 0; i < data_len; ++i) {
        uint8_t factor = data[i] ^ rem[0];
        memmove(rem, rem + 1, (size_t)(degree - 1));
        rem[degree - 1] = 0;
        for (int j = 0; j < degree; ++j) {
            rem[j] ^= gf_mul(coeff[j], factor);
        }
    }
}

static void qr_set_func(wifi_qr_t *qr, int x, int y, bool dark)
{
    if (x >= 0 && x < qr->size && y >= 0 && y < qr->size) {
        qr->modules[y][x] = dark ? 1 : 0;
        qr->is_func[y][x] = 1;
    }
}

static void qr_draw_finder(wifi_qr_t *qr, int cx, int cy)
{
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            int dist = (dx < 0 ? -dx : dx);
            int dy_abs = (dy < 0 ? -dy : dy);
            if (dy_abs > dist) {
                dist = dy_abs;
            }
            int xx = cx + dx;
            int yy = cy + dy;
            if (xx >= 0 && xx < qr->size && yy >= 0 && yy < qr->size) {
                qr->modules[yy][xx] = (dist != 2 && dist != 4) ? 1 : 0;
                qr->is_func[yy][xx] = 1;
            }
        }
    }
}

static void qr_draw_alignment(wifi_qr_t *qr, int cx, int cy)
{
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int dist = (dx < 0 ? -dx : dx);
            int dy_abs = (dy < 0 ? -dy : dy);
            if (dy_abs > dist) {
                dist = dy_abs;
            }
            qr_set_func(qr, cx + dx, cy + dy, dist != 1);
        }
    }
}

static void qr_draw_format_bits(wifi_qr_t *qr, int mask)
{
    /* ECC Level L = 01 */
    int data = (1 << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) {
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    }
    int bits = ((data << 10) | rem) ^ 0x5412;
    for (int i = 0; i <= 5; ++i) {
        qr_set_func(qr, 8, i, ((bits >> i) & 1) != 0);
    }
    qr_set_func(qr, 8, 7, ((bits >> 6) & 1) != 0);
    qr_set_func(qr, 8, 8, ((bits >> 7) & 1) != 0);
    qr_set_func(qr, 7, 8, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; ++i) {
        qr_set_func(qr, 14 - i, 8, ((bits >> i) & 1) != 0);
    }
    for (int i = 0; i < 8; ++i) {
        qr_set_func(qr, qr->size - 1 - i, 8, ((bits >> i) & 1) != 0);
    }
    for (int i = 8; i < 15; ++i) {
        qr_set_func(qr, 8, qr->size - 15 + i, ((bits >> i) & 1) != 0);
    }
    qr_set_func(qr, 8, qr->size - 8, true);
}

static void append_bits(uint32_t val, int num_bits, uint8_t *buf, int *bit_len)
{
    for (int i = num_bits - 1; i >= 0; --i) {
        if ((val >> i) & 1u) {
            buf[(*bit_len) >> 3] |= (uint8_t)(1u << (7 - ((*bit_len) & 7)));
        }
        ++(*bit_len);
    }
}

static void wifi_qr_encode(wifi_qr_t *qr, const char *text)
{
    size_t len = strlen(text);
    /* Version 3-L holds up to 53 bytes; Version 4-L holds up to 78 bytes. */
    int version = (len <= 53) ? 3 : 4;
    if (len > 78) {
        len = 78;
    }
    int data_codewords = (version == 3) ? 55 : 80;
    int ecc_codewords = (version == 3) ? 15 : 20;
    int total_codewords = data_codewords + ecc_codewords;

    memset(qr, 0, sizeof(*qr));
    qr->size = (uint8_t)(version * 4 + 17);

    uint8_t data[100];
    memset(data, 0, sizeof(data));
    int bit_len = 0;
    append_bits(0x4, 4, data, &bit_len);          /* Byte mode */
    append_bits((uint32_t)len, 8, data, &bit_len);
    for (size_t i = 0; i < len; ++i) {
        append_bits((uint8_t)text[i], 8, data, &bit_len);
    }
    int capacity_bits = data_codewords * 8;
    int term = capacity_bits - bit_len;
    if (term > 4) {
        term = 4;
    }
    append_bits(0, term, data, &bit_len);
    while ((bit_len & 7) != 0) {
        append_bits(0, 1, data, &bit_len);
    }
    for (uint8_t pad = 0xEC; bit_len < capacity_bits; pad ^= (0xEC ^ 0x11)) {
        append_bits(pad, 8, data, &bit_len);
    }

    uint8_t coeff[24];
    rs_compute_divisor(ecc_codewords, coeff);
    rs_get_remainder(data, data_codewords, coeff, ecc_codewords,
                     data + data_codewords);

    /* Function patterns */
    for (int i = 0; i < qr->size; ++i) {
        qr_set_func(qr, 6, i, (i % 2) == 0);
        qr_set_func(qr, i, 6, (i % 2) == 0);
    }
    qr_draw_finder(qr, 3, 3);
    qr_draw_finder(qr, qr->size - 4, 3);
    qr_draw_finder(qr, 3, qr->size - 4);
    qr_draw_alignment(qr, qr->size - 7, qr->size - 7);
    qr_draw_format_bits(qr, 0);

    /* Place codewords in zigzag */
    int i_bit = 0;
    for (int right = qr->size - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (int vert = 0; vert < qr->size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? (qr->size - 1 - vert) : vert;
                if (!qr->is_func[y][x] && i_bit < total_codewords * 8) {
                    bool dark = ((data[i_bit >> 3] >> (7 - (i_bit & 7))) & 1) != 0;
                    qr->modules[y][x] = dark ? 1 : 0;
                    ++i_bit;
                }
            }
        }
    }

    /* Apply mask 0: (x + y) % 2 == 0 */
    for (int y = 0; y < qr->size; ++y) {
        for (int x = 0; x < qr->size; ++x) {
            if (!qr->is_func[y][x] && ((x + y) % 2 == 0)) {
                qr->modules[y][x] ^= 1;
            }
        }
    }
    qr_draw_format_bits(qr, 0);
}

/* Standard QOI (https://qoiformat.org), channels=4. GSP decodes this with
 * gsp_codec_qoi_decode and does not touch ROM tinfl. */

#define QOI_OP_INDEX 0x00u
#define QOI_OP_DIFF  0x40u
#define QOI_OP_LUMA  0x80u
#define QOI_OP_RUN   0xC0u
#define QOI_OP_RGB   0xFEu

typedef struct {
    uint8_t r, g, b, a;
} qoi_px_t;

typedef struct {
    uint8_t *out;
    size_t cap;
    size_t pos;
    bool overflow;
    int run;
    qoi_px_t prev;
    qoi_px_t index[64];
} qoi_enc_t;

static void qoi_put(qoi_enc_t *enc, uint8_t byte)
{
    if (enc->pos >= enc->cap) {
        enc->overflow = true;
        return;
    }
    enc->out[enc->pos++] = byte;
}

static void qoi_begin(qoi_enc_t *enc, uint8_t *out, size_t cap, int width, int height)
{
    memset(enc, 0, sizeof(*enc));
    enc->out = out;
    enc->cap = cap;
    enc->prev.a = 255;
    qoi_put(enc, 'q');
    qoi_put(enc, 'o');
    qoi_put(enc, 'i');
    qoi_put(enc, 'f');
    qoi_put(enc, (uint8_t)((width >> 24) & 0xFF));
    qoi_put(enc, (uint8_t)((width >> 16) & 0xFF));
    qoi_put(enc, (uint8_t)((width >> 8) & 0xFF));
    qoi_put(enc, (uint8_t)(width & 0xFF));
    qoi_put(enc, (uint8_t)((height >> 24) & 0xFF));
    qoi_put(enc, (uint8_t)((height >> 16) & 0xFF));
    qoi_put(enc, (uint8_t)((height >> 8) & 0xFF));
    qoi_put(enc, (uint8_t)(height & 0xFF));
    qoi_put(enc, 4); /* RGBA */
    qoi_put(enc, 0); /* sRGB */
}

static void qoi_flush_run(qoi_enc_t *enc)
{
    while (enc->run > 0) {
        int chunk = enc->run > 62 ? 62 : enc->run;
        qoi_put(enc, (uint8_t)(QOI_OP_RUN | (uint8_t)(chunk - 1)));
        enc->run -= chunk;
    }
}

static void qoi_pixel(qoi_enc_t *enc, uint8_t r, uint8_t g, uint8_t b)
{
    qoi_px_t px = {r, g, b, 255};
    if (px.r == enc->prev.r && px.g == enc->prev.g &&
        px.b == enc->prev.b && px.a == enc->prev.a) {
        ++enc->run;
        if (enc->run == 62) {
            qoi_flush_run(enc);
        }
        return;
    }
    qoi_flush_run(enc);

    unsigned hash = ((unsigned)px.r * 3u + (unsigned)px.g * 5u +
                     (unsigned)px.b * 7u + (unsigned)px.a * 11u) % 64u;
    qoi_px_t seen = enc->index[hash];
    if (seen.r == px.r && seen.g == px.g && seen.b == px.b && seen.a == px.a) {
        qoi_put(enc, (uint8_t)(QOI_OP_INDEX | hash));
    } else {
        enc->index[hash] = px;
        int dr = (int)px.r - (int)enc->prev.r;
        int dg = (int)px.g - (int)enc->prev.g;
        int db = (int)px.b - (int)enc->prev.b;
        if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
            qoi_put(enc, (uint8_t)(QOI_OP_DIFF |
                                   ((dr + 2) << 4) |
                                   ((dg + 2) << 2) |
                                   (db + 2)));
        } else {
            int dr_dg = dr - dg;
            int db_dg = db - dg;
            if (dg >= -32 && dg <= 31 &&
                dr_dg >= -8 && dr_dg <= 7 &&
                db_dg >= -8 && db_dg <= 7) {
                qoi_put(enc, (uint8_t)(QOI_OP_LUMA | (dg + 32)));
                qoi_put(enc, (uint8_t)(((dr_dg + 8) << 4) | (db_dg + 8)));
            } else {
                qoi_put(enc, QOI_OP_RGB);
                qoi_put(enc, px.r);
                qoi_put(enc, px.g);
                qoi_put(enc, px.b);
            }
        }
    }
    enc->prev = px;
}

static size_t qoi_finish(qoi_enc_t *enc)
{
    qoi_flush_run(enc);
    for (int i = 0; i < 7; ++i) {
        qoi_put(enc, 0);
    }
    qoi_put(enc, 1);
    return enc->overflow ? 0 : enc->pos;
}

static void qr_module_color(const wifi_qr_t *qr, int side, int x, int y,
                             uint8_t *r, uint8_t *g, uint8_t *b)
{
    const int qr_size = qr->size > 0 ? qr->size : 29;
    /* Keep a 4px quiet zone, then use the largest module size that fits.
     * A fixed 8px budget left a version-3 code at scale 2 inside 92px,
     * which is a 17px white frame on every side. */
    int scale = (side - 4) / qr_size;
    if (scale < 2) {
        scale = 2;
    }
    if (qr_size * scale > side) {
        scale = side / qr_size;
        if (scale < 1) {
            scale = 1;
        }
    }
    const int total_px = qr_size * scale;
    const int margin = (side - total_px) / 2;
    int mx = x - margin;
    int my = y - margin;
    bool dark = false;
    if (mx >= 0 && my >= 0 && mx < total_px && my < total_px) {
        dark = qr->modules[my / scale][mx / scale] != 0;
    }
    *r = dark ? 0x07 : 0xFF;
    *g = dark ? 0x11 : 0xFF;
    *b = dark ? 0x1D : 0xFF;
}

static size_t wifi_qr_to_qoi_sized(const wifi_qr_t *qr, int side,
                                   uint8_t *out, size_t cap)
{
    qoi_enc_t enc;
    qoi_begin(&enc, out, cap, side, side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            uint8_t r, g, b;
            qr_module_color(qr, side, x, y, &r, &g, &b);
            qoi_pixel(&enc, r, g, b);
        }
    }
    return qoi_finish(&enc);
}

static size_t wifi_qr_to_qoi(const wifi_qr_t *qr, uint8_t *out, size_t cap)
{
    return wifi_qr_to_qoi_sized(qr, 92, out, cap);
}

#define SPEED_HISTORY_LEN 24

static void chart_sample_y(const uint16_t history[SPEED_HISTORY_LEN], uint8_t line_y[432])
{
    const int width = 432;
    uint16_t peak = 20;
    for (int i = 0; i < SPEED_HISTORY_LEN; ++i) {
        if (history[i] > peak) {
            peak = history[i];
        }
    }
    for (int x = 0; x < width; ++x) {
        int seg_scaled = x * (SPEED_HISTORY_LEN - 1);
        int idx = seg_scaled / (width - 1);
        int rem = seg_scaled % (width - 1);
        if (idx >= SPEED_HISTORY_LEN - 1) {
            idx = SPEED_HISTORY_LEN - 2;
            rem = width - 1;
        }
        int32_t v0 = history[idx];
        int32_t v1 = history[idx + 1];
        int32_t v = v0 + ((v1 - v0) * rem) / (width - 1);
        int32_t y = 28 - (v * 25) / (int32_t)peak;
        if (y < 3) {
            y = 3;
        } else if (y > 28) {
            y = 28;
        }
        line_y[x] = (uint8_t)y;
    }
}

static void chart_color_at(int y, int cy, int prev_cy, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int min_cy = cy < prev_cy ? cy : prev_cy;
    int max_cy = cy > prev_cy ? cy : prev_cy;
    *r = 0x07;
    *g = 0x11;
    *b = 0x1D;
    if (y >= min_cy && y <= max_cy) {
        *r = 0x29;
        *g = 0xB9;
        *b = 0xF4;
    } else if (y == min_cy - 1 || y == max_cy + 1) {
        *r = 0x12;
        *g = 0x4C;
        *b = 0x6E;
    } else if (y > max_cy && y <= 28) {
        *r = 0x0B;
        *g = 0x24;
        *b = 0x38;
    } else if (y == 28) {
        *r = 0x14;
        *g = 0x2A;
        *b = 0x3E;
    }
}

/* 432x32 QOI of the live download history, in 0.1 Mbps units.
 * A zero history stays on the baseline at y=28. */
static size_t speed_chart_to_qoi(const uint16_t history[SPEED_HISTORY_LEN],
                                 uint8_t *out, size_t cap)
{
    const int width = 432;
    const int height = 32;
    uint8_t line_y[432];
    chart_sample_y(history, line_y);

    qoi_enc_t enc;
    qoi_begin(&enc, out, cap, width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int cy = (int)line_y[x];
            int prev_cy = (x > 0) ? (int)line_y[x - 1] : cy;
            uint8_t r, g, b;
            chart_color_at(y, cy, prev_cy, &r, &g, &b);
            qoi_pixel(&enc, r, g, b);
        }
    }
    return qoi_finish(&enc);
}
