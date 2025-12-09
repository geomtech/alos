/* Configuration FreeType pour ALOS - Environnement freestanding */

#ifndef FTOPTION_H_
#define FTOPTION_H_

/* Désactiver les fonctionnalités non nécessaires pour réduire la taille */
#undef FT_CONFIG_OPTION_SYSTEM_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_HARFBUZZ

/* Activer seulement les formats nécessaires */
#define FT_CONFIG_OPTION_ENABLE_GX_VAR_SUPPORT

/* Désactiver le support des fichiers (on charge depuis la mémoire) */
/* Pas de FT_CONFIG_OPTION_USE_XXX pour les fichiers */

#endif /* FTOPTION_H_ */

