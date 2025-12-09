# FreeType pour ALOS

Ce répertoire contient les sources FreeType nécessaires pour le rendu de polices TrueType.

## Compilation

FreeType doit être compilé en mode freestanding pour ALOS. Les sources sont dans `src/` et les headers dans `include/`.

### Configuration minimale

Pour compiler FreeType pour ALOS, vous devez:

1. Configurer FreeType avec les options minimales
2. Compiler avec les flags freestanding
3. Lier avec le reste du code GUI

### Modules nécessaires

Les modules FreeType suivants sont nécessaires:
- `autofit` - Auto-hinting
- `truetype` - Support TTF
- `smooth` - Rendu anti-aliased
- `psnames` - Noms de polices PostScript

### Note

Pour un environnement freestanding complet, il peut être nécessaire d'adapter certaines parties de FreeType qui dépendent de la libc standard.

