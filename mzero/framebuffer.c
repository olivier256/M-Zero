#include "framebuffer.h"

#include "bigendian.h"

/* La table de lignes est lue par le VDP comme des mots, alors que
   vdp_dram_write16() range les octets dans l'ordre du SH2 (pour les
   pixels) : sur un hôte petit-boutiste, on lui passe donc l'entrée déjà
   retournée (BE16). Sur SH2, BE16 ne fait rien. Voir pixel/bigendian.h. */
void fb_set_line(int y, int line) {
    vdp_dram_write16((uint32_t)y * 2, BE16(fb_line_address(line)));
}

void fb_init(int screenHeight) {
    for (int y = 0; y < screenHeight; y++)
        fb_set_line(y, y);
    for (int x = 0; x < FB_WIDTH; x += 2)
        vdp_dram_write16(fb_offset(x, FB_BLANK_LINE), 0);
}
