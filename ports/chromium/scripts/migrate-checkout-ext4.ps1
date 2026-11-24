param(
    [Parameter(Mandatory)][string]$ChromiumDirectory,
    [string]$Volume = "alos-chromium"
)
# Copie un checkout Chromium (hors out/) du disque Windows vers un volume Docker
# ext4. Ne supprime rien. Puis utiliser le chemin affiche comme -ChromiumDirectory
# de build-chromium.ps1 (voir ports/chromium/BUILD-SPEED.md). Duree : ~35-60 min
# pour ~8 Go / ~500 000 fichiers (la lecture NTFS est le facteur limitant).
$ErrorActionPreference = "Stop"
$src = (Resolve-Path $ChromiumDirectory).ProviderPath
& docker volume create $Volume | Out-Null
$count = & docker run --rm -v "${Volume}:/d" alos-runtime sh -c "ls -A /d | wc -l"
if ([int]$count -ne 0) { throw "Le volume $Volume n'est pas vide : abandon (rien n'est ecrase)." }
& docker run --rm -v "${src}:/s:ro" -v "${Volume}:/d" alos-runtime sh -c `
    "cd /s && tar cf - --exclude=./out . | (cd /d && tar xf -)"
if ($LASTEXITCODE) { throw "Copie echouee." }
$unc = "\wsl`$\docker-desktop\mnt\docker-desktop-disk\data\docker\volumes\$Volume\_data"
Write-Host "Checkout ext4 pret. Utiliser : -ChromiumDirectory '$unc'"
