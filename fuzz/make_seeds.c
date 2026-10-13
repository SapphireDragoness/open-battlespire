/*
 * Writes synthetic seed inputs (no game data) for each fuzzer:
 *   make_seeds <corpus-root>  ->  <corpus-root>/<target>/<seed>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bs/core/fs.h"
#include "synth.h"

static int write_seed(const char *root, const char *target, const char *name, const void *data, size_t n)
{
    char dir[BS_PATH_MAX], path[BS_PATH_MAX];
    if (n == 0 || !bs_path_join(dir, sizeof dir, root, target) || bs_fs_mkdirs(dir) != BS_OK ||
        !bs_path_join(path, sizeof path, dir, name) || bs_fs_write_file(path, data, n) != BS_OK) {
        fprintf(stderr, "make_seeds: failed to write %s/%s\n", target, name);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: make_seeds <corpus-root>\n");
        return 2;
    }
    const char *root = argv[1];
    static uint8_t buf[SYNTH_ISO_SECTORS * 2352], iso[SYNTH_ISO_SIZE];
    int err = 0;
    size_t n;

    n = synth_bsa(buf, sizeof buf);
    err |= write_seed(root, "bsa", "named", buf, n);
    err |= write_seed(root, "lzss", "comp", synth_comp_data, sizeof synth_comp_data);
    n = synth_bsa_numbered(buf, sizeof buf);
    err |= write_seed(root, "bsa", "numbered", buf, n);
    n = synth_rsc(buf, sizeof buf);
    err |= write_seed(root, "rsc", "small", buf, n);
    n = synth_le(buf, sizeof buf);
    err |= write_seed(root, "le", "small", buf, n);

    n = synth_bsi_single(buf, sizeof buf);
    err |= write_seed(root, "bsi", "single", buf, n);
    n = synth_bsi_container(buf, sizeof buf);
    err |= write_seed(root, "bsi", "container", buf, n);

    n = synth_model(buf, sizeof buf);
    err |= write_seed(root, "model", "small", buf, n);
    n = synth_bs6(buf, sizeof buf);
    err |= write_seed(root, "bs6", "small", buf, n);
    n = synth_flc(buf, sizeof buf);
    err |= write_seed(root, "flic", "small", buf, n);
    n = synth_font(buf, sizeof buf);
    err |= write_seed(root, "font", "small", buf, n);

    size_t iso_n = synth_iso(iso, sizeof iso);
    err |= write_seed(root, "cdimage", "plain.iso", iso, iso_n);
    n = synth_raw_mode2(iso, iso_n, buf, sizeof buf);
    err |= write_seed(root, "cdimage", "raw.bin", buf, n);
    static const char cue[] = "FILE \"game.gog\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
    err |= write_seed(root, "cdimage", "cue", cue, sizeof cue - 1);

    static const char levels[] =
        "l1.bs6\t\tLv1\t\t1336\tNNF\txxxx\t\t555\t1024\tnodes.l1\t1500\t4096\t80\tThe Weir Gate\r\n"
        "dmkk.bs6\tCf1\t\t2000\tCNC\tsky1.bsi\t444\t31000\txxxx\t\t2000\t3072\t100\tCastle flag capture\r\n";
    err |= write_seed(root, "levels", "two", levels, sizeof levels - 1);

    static const uint8_t lz[] = { 0xFF, 'v', '2', '.', '7', 0, 0, 0, 0, 0x01, 'x', 0xEE, 0xFF, 0x00, 0x00, 0x02 };
    err |= write_seed(root, "lzss", "mixed", lz, sizeof lz);
    return err;
}
