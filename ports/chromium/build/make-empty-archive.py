"""Ecrit une archive ar vide (en-tete seul) : bibliotheque de liaison sans symbole.

Sert a satisfaire un `-l<nom>` ajoute par la config amont pour une
bibliotheque que ALOS n'a pas (ex. libdl) sans en simuler aucune fonction :
si le code appelait vraiment dlopen/dlsym, l'edition de liens echouerait."""
import os
import sys

output = sys.argv[1]
os.makedirs(os.path.dirname(output), exist_ok=True)
with open(output, "wb") as stream:
    stream.write(b"!<arch>\n")
