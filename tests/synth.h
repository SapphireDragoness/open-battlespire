#ifndef BS_TESTS_SYNTH_H
#define BS_TESTS_SYNTH_H

/*
 * Synthetic test inputs, built from scratch (no game data). Shared by the
 * unit tests and by fuzz/make_seeds.c, which writes them as fuzzing seeds.
 * Each builder writes into out (capacity cap) and returns the size, or 0 if
 * cap is too small.
 */

#include <stddef.h>
#include <stdint.h>

/* Named BSA: "HELLO.TXT" = "hello" (stored), "COMP.3D" = "a" + 5 spaces (LZSS, 4 bytes). */
#define SYNTH_BSA_SIZE 49
extern const uint8_t synth_comp_data[4];
size_t synth_bsa(uint8_t *out, size_t cap);

/* Numbered BSA: id 102 = "RIFF", id 507 = "xyz". */
size_t synth_bsa_numbered(uint8_t *out, size_t cap);

/*
 * ISO 9660 image, 2048-byte sectors, volume "TESTVOL":
 *   /HELLO.TXT          "hello world"
 *   /SUBDIR/BIG.BIN     SYNTH_ISO_BIG_SIZE bytes, byte i = (i * 7) & 0xFF, spans a sector boundary
 */
#define SYNTH_ISO_SECTORS 23
#define SYNTH_ISO_SIZE (SYNTH_ISO_SECTORS * 2048)
#define SYNTH_ISO_BIG_SIZE 3000
size_t synth_iso(uint8_t *out, size_t cap);

/* Wraps a 2048-byte-sector ISO as raw Mode 2 Form 1 sectors (2352 bytes each). */
size_t synth_raw_mode2(const uint8_t *iso, size_t iso_size, uint8_t *out, size_t cap);

/* TEXT.RSC: 5 = "Hi" FD "there", 9 = empty, 12 = "a" FA 00 "b", then the 0xFFFF sentinel. */
size_t synth_rsc(uint8_t *out, size_t cap);

/*
 * BSI files.
 *
 * synth_bsi_single: one image, no container: 2x2, raw, transparent (BHDR 4 = 3),
 *   NAME "ONE", HICL[1] = 0xF800 (red), pixels {0, 2, 2, 0}.
 *
 * synth_bsi_container: "BSIF" container with two images and an END chunk with a
 * garbage length (as in some shipped files):
 *   [0] 1x1 raw, pixel {4}
 *   [1] 4x2, 2 frames, encoding 6 (row offset table):
 *       frame 0 row 0  RLE  {2,2,2,2}
 *       frame 0 row 1  raw  {0,2,4,6}
 *       frame 1 row 0  RLE  {8,10,0,0}   (literal 2 + run of 2)
 *       frame 1 row 1  raw, shares frame 0 row 1's bytes
 */
size_t synth_bsi_single(uint8_t *out, size_t cap);
size_t synth_bsi_container(uint8_t *out, size_t cap);

/*
 * Font with 3 glyphs: '!' 2x1 = {0x7FFF, 0x0000}; '"' 1x1 sharing '!' pixels;
 * '#' absent (offset = file size).
 */
size_t synth_font(uint8_t *out, size_t cap);

/*
 * FLC, 4x2, 2 frames + ring frame, palette entries 0 = black, 1 = red, 2 = green:
 *   frame 1 (COLOR_256 + BYTE_RUN):  row 0 {1,1,1,1}, row 1 {0,1,0,1}
 *   frame 2 (DELTA_FLC, skips row 0): row 1 {0,1,2,2}
 *   ring    (DELTA_FLC): back to frame 1
 */
size_t synth_flc(uint8_t *out, size_t cap);

/*
 * LE executable (MZ stub + LE), page size 512, 3 pages:
 *   object 1: base 0x10000, 2 pages, r-x; entry at +8
 *   object 2: base 0x20000, 1 page (last page 0x100 bytes in file), virtual size 0x1000, rw-
 * Page bytes are 0xA1 (obj1 page 1), 0xA2 (obj1 page 2), 0xB0 (obj2) before fixups.
 * Fixups (object offsets):
 *   obj1+0x010  offset32  -> obj2+0x20                 = 0x00020020
 *   obj1+0x020  rel32     -> obj1+0x100                = 0x10100 - 0x10024
 *   obj1+0x030, obj1+0x040  list, additive 5 -> obj2   = 0x00020005
 *   obj1+0x1FE  offset32 spanning pages 1 and 2 (in both page lists) -> obj1+0x44 = 0x00010044
 *   obj1+0x210  selector -> obj2 (not applied)
 *   obj2+0x000  offset32  -> obj1+0x1                  = 0x00010001
 */
size_t synth_le(uint8_t *out, size_t cap);

/* Packs a texture name (up to 6 characters) as a 3D model face texture value. */
uint32_t synth_base40(const char *name);

/*
 * 3D model v2.7: 4 points (0,0,0) (512,0,0) (512,512,0) (0,512,0), 3 faces,
 * all with normal (0,0,256):
 *   [0] quad 0 1 2 3, texture "WALL96", stored UVs (16,32) (+160,0) (+0,+160)
 *       and garbage for the 4th; texels (1,2) (11,2) (11,12), derived (1,12)
 *   [1] triangle 0 1 2, colour 0xFFFFF800 (red)
 *   [2] triangle 0 2 3, invisible (0xFFFFFFFE)
 */
size_t synth_model(uint8_t *out, size_t cap);

/*
 * BS6 level: BBOX (-100,-50,-100)-(100,50,100), WATR 91,
 *   VIEW id 0 type 6 at (1,2,3) angles (256,0,0) ambient 16000
 *   OBJS: LFIL {"box", "rug"};
 *     OBJD id 7 file 1 at (10,20,30) angles (512,0,0), with a CTRL chunk
 *     OBJD id 8 file 0 at (-5,0,5) angles (0,1024,0)
 *   LITS: LITD id 1 at (0,-40,0) radius 300 brightness 40
 *   FLAS: FLAD id 2 "start" at (4,5,6)
 */
size_t synth_bs6(uint8_t *out, size_t cap);

/* WAV: PCM 8-bit mono 11025 Hz with an extra LIST chunk; 10 samples 128..137. */
size_t synth_wav(uint8_t *out, size_t cap);

/*
 * Smacker: SMK2, 4x4, 2 frames at 100 ms. Trees: MMAP/MCLR/FULL absent, TYPE a
 * single leaf 0x5503 (solid block, colour 0x55). Frame 0 sets palette entry
 * 0x55 to (63, 0, 0); frame 1 has 4 bytes of uncompressed audio {1, 2, 3, 4}
 * on track 0 (8-bit mono 11025 Hz).
 */
size_t synth_smk(uint8_t *out, size_t cap);

/*
 * NODES file with 3 nodes:
 *   0 at (1, -2, 3), group 7, links to 1 (distance 10) and 2 (distance 20)
 *   1 at (11, -2, 3), group 7, no links
 *   2 at (1, -2, 23), group 9, links back to 0 (distance 20)
 */
size_t synth_nodes(uint8_t *out, size_t cap);

/*
 * BS6 level exercising the logic chunks (models "box", "rug"; all objects use box):
 *   #10 button at (0,0,0): CTRL 16 (use), STRU 121 [72, 1], LINK 0 -> #11
 *   #11 door at (100,0,0): CTRL 512 [0, -512, 0, 1000 ms, "r"]
 *   #12 lift at (200,0,0): CTRL 5 (step on), CTRL 261 [0, -500, 0, 2000 ms, "r"]
 *   #13 wheel at (300,0,0): CTRL 16, CTRL 512 [0, 2047, 0, 500 ms, "c"]
 *   #14 portal at (400,0,0): CTRL 5, LINK 2 -> flat #20
 *   #15 at (500,0,0): CTRL 261 [10, 0, 0, 100 ms, ""], CTRL 149 [0, 300], LINK 0 -> #16
 *   #16 at (600,0,0): CTRL 261 [0, 10, 0, 100 ms, ""], CTRL 149 [0, 300], LINK 0 -> #15
 *   #17 one-shot at (700,0,0): CTRL 16, CTRL 261 [0, 0, 50, 100 ms, ""]
 *   flat #20 "user" at (1000,0,0): CTRL 261 [50, -20, 30, 0 ms, ""]
 *   flat #21 "start" at (0,-100,0)
 */
size_t synth_bs6_logic(uint8_t *out, size_t cap);

#endif
