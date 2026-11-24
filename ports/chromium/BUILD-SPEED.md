# Accelerer les builds Chromium/V8 pour ALOS

Mesures du 2026-11-24 (Docker Desktop, WSL2) : 16 CPU / 16 Go ; checkout et
`out/` sur NTFS monte par bind-mount ; pas de cache ; `-g2 -gsplit-dwarf` par
defaut ; `ninja -j 8`. Les gains ci-dessous sont des **estimations**, pas des
mesures sur ce depot (voir « Mesures » en bas pour les mettre a jour).

## Applique dans `build-chromium.ps1`

| Option | Defaut | Effet |
| --- | --- | --- |
| `-Jobs N` | 14 (avant : 8) | Parallelisme `ninja`. 16 CPU, 16 Go : 14 laisse de la RAM aux liens lourds. |
| `-SymbolLevel N` | 0 | `symbol_level`, `blink_symbol_level`, `v8_symbol_level` : 0 = pas d'infos de debug (compilation plus rapide, liens plus rapides, `out/` ~5x plus petit). Mettre 1 ou 2 pour gdb. Changer cette valeur invalide les objets existants (rebuild complet). |
| `-Ccache` | off | `cc_wrapper = "ccache"`, image `alos-runtime-ccache` (`ports/chromium/build/Dockerfile.ccache`, construite une fois), cache sur le volume Docker `alos-ccache` (30 Go max). Utile aux recompilations a flags identiques (nouveau profil, changement d'args, `ninja -t clean`). |

Exemples :

```powershell
.\ports\chromium\scripts\build-chromium.ps1 -ChromiumDirectory <checkout> -V8Probe -Ccache
.\ports\chromium\scripts\build-chromium.ps1 -ChromiumDirectory <checkout> -Skia -SymbolLevel 2   # avec debug
docker run --rm -v alos-ccache:/ccache -e CCACHE_DIR=/ccache alos-runtime-ccache ccache -s      # statistiques
```

## Autres leviers (non appliques ici)

* **Profils** : `out/alos`, `alos-v8full`, `alos-runloop`, `alos-net`,
  `alos-skia` recompilent chacun `base`, `abseil`, `boringssl`, `perfetto` car
  leurs args different. Unifier les args en un profil de dev supprimerait
  3 a 4 recompilations. `-Ccache` en recupere l'essentiel sans changer les profils.
* **En-tetes libc partages** (`mman.h`, `syscall.h`) : un changement relance
  ~1800 compilations V8. Les regrouper.
* **Exclusions Windows Defender** sur le dossier de travail et les donnees
  Docker : gain possible de 10 a 30 % sur les E/S NTFS ; decision de securite de
  l'utilisateur, non appliquee.

## Mesures a refaire apres application

Comparer `ninja -d stats` / `.ninja_log` (temps de la meme cible, avant/apres),
`ccache -s` (taux de hits) et la taille de `out/`.
