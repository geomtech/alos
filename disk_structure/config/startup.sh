# /config/startup.sh - Script de démarrage ALOS
#
# Exécuté une seule fois au boot par /bin/sh (userland), avant que le shell
# interactif ne démarre. Chaque ligne est traitée exactement comme si elle
# avait été tapée au clavier (mêmes builtins, mêmes commandes externes).
#
# Les lignes vides et celles commençant par '#' ou ';' sont ignorées.

echo startup-begin
pwd
echo startup-end
