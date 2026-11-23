# ALOS Browser (alosweb) : premiere etape vers Chromium

## Pourquoi pas Chromium directement

L'audit de `docs/chromium-port.md` montre que le portage est a l'etape du
runtime (libc, pthread, epoll, sockets, libc++) : `//base` compile, mais
Blink, V8, Mojo/ipcz, le processus GPU/renderer et un backend Ozone/Views
pour le compositeur ALOS n'existent pas encore. Obtenir « une page qui
s'affiche » avec Chromium passe par plusieurs jalons longs (liaison de
`base`, `net`, `content`, `v8`, `blink`, Ozone). Pour avoir un navigateur
utilisable des maintenant, on ajoute un moteur minimal natif, **sans
remplacer ni retarder** le chantier Chromium.

## Ce qui existe

`src/userland/browser/` :

| Fichier | Role |
| --- | --- |
| `web.h` | API du moteur : `web_fetch` -> `web_layout` -> `web_render` |
| `url.c` | Analyse/resolution d'URL (http, https, file, about) |
| `http.c` | Client HTTP/1.1 sur sockets (DNS, redirections, chunked), `file:` et pages `about:` |
| `html.c` | Tokenizer HTML tolerant + mise en page en flux (titres, paragraphes, listes, liens, `pre`, `blockquote`, tableaux sommaires, entites) |
| `render.c` | Rendu logiciel ARGB avec la police 8x16 du desktop |
| `main.c` | Application GUI (`libgui`) : barre d'adresse, retour, recharger, liens cliquables, defilement, redimensionnement |
| `web-test.c` | Tests du moteur, executables sur l'hote |

Installe en `/bin/browser` et visible dans le lanceur
(`disk_structure/share/applications/browser.desktop`). Page par defaut :
`about:home`.

## Limites assumees

- Pas de CSS, JavaScript, images, formulaires ni polices autres que ASCII 8x16.
- **HTTPS non supporte** (pas de TLS) : un message d'erreur clair s'affiche.
  Sous QEMU, utiliser un serveur HTTP de l'hote (`http://10.0.2.2:8000/`).
- Chargement bloquant dans le thread GUI.
- Valide : build ALOS (libc/libgui reelle, edition de liens), tests hote
  (ASan/UBSan, serveur HTTP local avec redirection et chunked). **Pas encore
  execute dans la VM ALOS** : a verifier avec `run.ps1`.

## Tests

```sh
cd src/userland && make browser-host-test      # tests unitaires + about:test
./browser/web-test --ppm page.ppm http://127.0.0.1:8000/   # rendu d'une URL en PPM
make browser                                   # binaire ALOS
```

## Trajectoire vers Chromium / UI web

1. **Maintenant** : `alosweb` donne une application navigateur fonctionnelle
   et une API `fetch/layout/render` isolee de la fenetre.
2. **Chromium** : poursuivre `docs/chromium-port.md` (base -> net -> Mojo ->
   content_shell avec backend Ozone parlant `libgui`/SHM). Quand
   `content_shell` affiche une page, `main.c` bascule sur lui comme moteur
   (meme barre d'adresse/historique), `alosweb` restant comme repli leger.
3. **UI de l'OS en web** : les panneaux du desktop (lanceur, barre des
   taches) deviendront des pages locales `file:///` ou `about:` rendues par
   le meme moteur ; `alosweb` peut servir de prototype pour cette separation
   donnees/presentation avant Chromium.
4. Suites possibles pour `alosweb` : CSS minimal (couleurs, marges), images
   BMP/PNG, formulaires, TLS (mbedTLS).
