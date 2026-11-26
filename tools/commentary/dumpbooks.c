/* dumps every Wwise library codebook (standard + aoTuV603 lists) rebuilt to a standard Vorbis codebook by vgmstream's own
   code: "<list> <id> <bits> <hex of the bytes>" per line (bits: the rebuilt codebook's length in bits) */
#include "../../../wt/commentary2/exe/WWFNoMercyRecomp/src/main/commentary/packbuild/vgmstream/coding/vorbis_custom_utils_wwise.c"
#include <stdio.h>
int main(void) {
    for (int list = 0; list < 2; list++) {
        const vcb_info_t* l = list ? vcb_list_aotuv603 : vcb_list_standard;
        int n = list ? vcb_list_count_aotuv603 : vcb_list_count_standard;
        for (int i = 0; i < n; i++) {
            static uint8_t obuf[0x10000]; memset(obuf, 0, sizeof obuf);
            bitstream_t ow; bl_setup(&ow, obuf, sizeof obuf);
            STREAMFILE sf = {0};
            if (!ww2ogg_codebook_library_rebuild_by_id(&ow, l[i].id, list ? WWV_AOTUV603_CODEBOOKS : WWV_EXTERNAL_CODEBOOKS, &sf)) { printf("%d %u FAIL\n", list, l[i].id); continue; }
            int bits = bl_pos(&ow);
            printf("%d %u %d ", list, l[i].id, bits);
            for (int k = 0; k < (bits + 7) / 8; k++) printf("%02x", obuf[k]);
            printf("\n");
        }
    }
    return 0;
}
