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
    n = synth_bs6_logic(buf, sizeof buf);
    err |= write_seed(root, "bs6", "logic", buf, n);
    n = synth_wav(buf, sizeof buf);
    err |= write_seed(root, "wav", "small", buf, n);
    n = synth_smk(buf, sizeof buf);
    err |= write_seed(root, "smk", "small", buf, n);
    n = synth_nodes(buf, sizeof buf);
    err |= write_seed(root, "nodes", "small", buf, n);
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
    static const char magic[] = "Name\t\"of the Test\"\r\nID#\t7001\r\nItem\t2\r\nSpell\t-1, 28, -1, 8, 4\r\n"
                                "App\t15, 30, 100\r\nAdv\t36, 37\r\nUses\t10\r\nLevel\t0\r\n\r\n";
    err |= write_seed(root, "magic", "one", magic, sizeof magic - 1);
    n = synth_item_exe(buf, sizeof buf);
    err |= write_seed(root, "magic", "tables", buf, n);
    static const char book[] = "[[Heading]]\r\n\r\nHello %PCF, I am %APF.^Next$line\r\nmore text here";
    err |= write_seed(root, "book", "small", book, sizeof book - 1);

    /* A save tree: the version, a player, an item in it, the end, then two idle controllers and none running. */
    {
        static uint8_t tree[4 + 2 * 4 + 0x3D * 2 + 0x317 + 0x2F3 + 4 + 2 + 4];
        memset(tree, 0, sizeof tree);
        size_t at = 0;
        tree[1] = 1; /* 0x100 */
        at += 4;
        const uint32_t sizes[2] = {0x3D + 0x317, 0x3D + 0x2F3};
        for (int r = 0; r < 2; r++) {
            uint8_t *p = tree + at;
            p[0] = (uint8_t)sizes[r];
            p[1] = (uint8_t)(sizes[r] >> 8);
            p[4] = r == 0 ? 3 : 2;
            p[4 + 0x1D] = r == 0 ? 0x50 : 0x11;     /* ids 50000 and 10001 */
            p[4 + 0x1E] = r == 0 ? 0xC3 : 0x27;
            if (r == 1) {
                p[4 + 0x39] = 0x50;                 /* in the player */
                p[4 + 0x3A] = 0xC3;
            }
            at += 4 + sizes[r];
        }
        at += 4 + 2 + 4;
        err |= write_seed(root, "save", "tree", tree, at);
    }
    {
        /* One of our saves, small: a player, an item in a sack, a monster holding one, an automap. */
        static const char text[] = "[save]\nversion = 1\nname = \"seed #1\"\nlevel = l1.bs6\nlevel_number = 1\n"
                                   "clock = 1000\n[player]\nname = A\nstrength = 50\nhotkeys = 21 0 0 0 0 0 0 0\n"
                                   "[spell]\nnumber = 21\nicon = 5\n[item]\nid = 1\nbase = 16\nwhere = carried\n"
                                   "[item]\nid = 2\nbase = 22\nquantity = 9\nwhere = item 1\n[monster]\nid = 577\n"
                                   "creature = 2\npos = 1 2 3\n[item]\nid = 3\nbase = 31\nwhere = monster 577\n"
                                   "[automap]\nmap = 0\nrow = x.......................................\n"
                                   "[variables]\nPCMale = 1\n@123 = 2\n[locals]\nid = 577\nGotgem = 1\n";
        err |= write_seed(root, "game_save", "small", (const uint8_t *)text, sizeof text - 1);
    }

    static const char talk[] = "\ttalkcheck\r\n\t&Mad\tXX1A31\r\n\t\r\n\ttalktable\r\n\tFirst\tXX1A21\r\n"
                               "\tXX1A21\tHello.\tXX1A2A\tTRUE\t\t\tHi.\t01-50: XX1A31 51-00: END\tPUT 21_033\t&Mad\r\n"
                               "\tXX1A31\t[[It glares.]]\tXX1A3A\tnot &Mad\t\t\tBye.\tEND\tMonsterAttack\r\n";
    err |= write_seed(root, "talk", "small", talk, sizeof talk - 1);

    static const uint8_t lz[] = { 0xFF, 'v', '2', '.', '7', 0, 0, 0, 0, 0x01, 'x', 0xEE, 0xFF, 0x00, 0x00, 0x02 };
    err |= write_seed(root, "lzss", "mixed", lz, sizeof lz);
    return err;
}
