/* =========================================================================
   Démo : un morceau de 1024 x 1024 pixels du circuit Big Blue (F-Zero,
   Super Nintendo), recalé sur sa grille de tiles (voir
   tools/crop2bmp.py) : 110 tiles différentes.
   ========================================================================= */

#include "fzero.h"

#include "demo_assets.h" /* généré par bmp2m7 depuis demo/assets/bigblue.bmp */

const MZeroContent mzero_content = {
    .plane = &asset_bigblue,
    .palette = asset_palette,
};
