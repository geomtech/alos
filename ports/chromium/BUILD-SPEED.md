# Accelerer les builds Chromium/V8 pour ALOS

**Resultat principal (2026-11-24) : tout sur ext4 = build 3x plus rapide, rebuild avec ccache ~100x.** Voir « Mesures ».

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

## Mode ext4 (recommande)

Cause du ralentissement mesuree : les **en-tetes lus sur des bind-mounts NTFS**
(`/libcxx`, `/alos`, sources Chromium) : une compilation servie par ccache coutait
4,6 s (hashage de milliers d'en-tetes via 9p) contre 0,011 s sur ext4. Le CPU
n'etait pas le facteur limitant.

1. Copier le checkout (une fois, sans rien supprimer) :
   `.\ports\chromium\scripts\migrate-checkout-ext4.ps1 -ChromiumDirectory <checkout NTFS>`
   (volume Docker `alos-chromium` ; chemin Windows du volume :
   `\wsl$\docker-desktop\mnt\docker-desktop-disk\data\dockerolumeslos-chromium\_data`).
2. Builder avec ce chemin comme `-ChromiumDirectory` : `build-chromium.ps1` detecte
   le volume, monte `alos-chromium:/chromium`, `alos-libcxx:/libcxx` (copie unique
   de `-LibcxxDirectory`) et `alos-repo:/alos` (synchronise a chaque build :
   `src/include`, `src/userland/{libc,libm,libgui}`, `ports/chromium`). Les
   operations hote (git, gabarits) passent par le chemin UNC.
3. Les binaires sont lisibles depuis Windows par le meme chemin UNC :
   `test-vm.ps1 -V8SmokePath "<UNC>\outlos-skia\skia_raster_smoke" ...`.

Exemple : `.\ports\chromium\scriptsuild-chromium.ps1 -ChromiumDirectory
'\wsl$\docker-desktop\mnt\docker-desktop-disk\data\dockerolumeslos-chromium\_data'
-LibcxxDirectory <alos-libcxx-21 NTFS> -Skia -Ccache`.

Limites : le build ALOS doit changer `src/userland/libc` etc. cote Windows ; la
synchro vers `alos-repo` est automatique a chaque `build-chromium.ps1`, mais pas
pour un `ninja` lance a la main. L'ancien checkout NTFS reste intact (rien n'est
supprime) ; les volumes occupent ~8 Go (checkout) + out/ + cache.

## Mesures (base:base, 2549 actions, 16 CPU / 16 Go)

| Configuration | Temps |
| --- | --- |
| A avant : `-g2`, `-j 8`, tout sur NTFS | ~848 s |
| B froid : `symbol_level=0`, `-j 14`, ccache vide, NTFS | ~1977 s (surcout ccache + I/O) |
| B chaud : ccache 100 % hits, NTFS | ~621 s |
| B chaud + `out/` seul sur volume ext4 | ~521 s |
| B chaud, sources + `out/` ext4, libcxx/libc sur NTFS | ~428 s |
| **B chaud, tout sur ext4** | **~6 s** |
| **C reel : sans ccache, `symbol_level=0`, `-j 14`, tout sur ext4** | **~277 s (3x plus rapide que A)** |

`out/` : 528 Mo (`-g2`) -> 101 Mo (`symbol_level=0`). Les estimations de gain
donnees avant ces mesures (25-40 % pour `symbol_level`, etc.) ne sont pas
confirmees isolement : le facteur dominant est l'E/S NTFS, pas le CPU ni les
symboles.

## Mesures a refaire apres application

Comparer `ninja -d stats` / `.ninja_log` (temps de la meme cible, avant/apres),
`ccache -s` (taux de hits) et la taille de `out/`.
