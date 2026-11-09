# TempleRush pour The Slormancer

*English version: [README.md](README.md)*

Récompense les étages terminés vite dans **Le Temple du Slorm** : plus tu termines un étage vite,
plus tu débloques d'étages.

| Temps pour terminer l'étage | Étages débloqués |
|-----------------------------|------------------|
| Plus de 2 minutes | 1 (la progression normale du jeu) |
| Moins de 2 minutes | 2 (+1, progression double) |
| Moins de 30 secondes | 6 (+5, progression multipliée par six) |

Exemple : si l'étage 61 est ton plus haut étage terminé et que tu termines l'étage 62 en moins de
2 minutes, c'est l'étage 64 qui devient disponible au lieu du 63.

Le bonus ne s'applique que la première fois que tu termines un étage (nouveau record). Rejouer un
étage déjà terminé ne donne aucun bonus. L'exécutable du jeu n'est jamais modifié.

## Fonctionnement

Le chrono démarre au début de l'étage. Quand le jeu termine l'étage, il enregistre le plus haut étage
terminé (`slorm_temple_data` dans le jeu, `temple_data` dans le fichier de sauvegarde). TempleRush
ajoute alors le bonus à cette valeur, juste avant la sauvegarde.

Le texte « Étage le plus élevé » en haut du menu du Temple est le compteur propre au jeu : il ne
comprend pas le bonus. Les étages que tu peux choisir, eux, le comprennent.

## Installation

[Aurie Framework](https://github.com/AurieFramework/Aurie) et
[YYToolkit](https://github.com/AurieFramework/YYToolkit) **v5.0.0c** doivent déjà être installés dans
le jeu.

Copie `x64/Release/TempleRush.dll` dans `The Slormancer\mods\Aurie\` (jeu fermé).

Pour désinstaller, supprime `TempleRush.dll`. Les étages déjà débloqués le restent.

## Réglages

Aucun : les temps (120 s et 30 s) et les bonus (+1 et +5) sont fixés dans le code. Pour les changer,
modifie les constantes en haut de `source/ModuleMain.cpp` puis recompile.

Le chrono compte au maximum 0,25 s par image : une pause compte très peu, mais ne l'arrête pas
complètement.

## Compilation

Visual Studio 2022 ou plus récent, avec « Développement Desktop en C++ ». Ouvre `TempleRush.vcxproj`
et compile en **Release / x64**. Les en-têtes Aurie et YYToolkit v5.0.0c sont dans `include/`.

## Licence

AGPL-3.0, voir [LICENSE](LICENSE). Le dossier `include/` contient les en-têtes Aurie et YYToolkit
(AGPL-3.0, AurieFramework).

Mod non officiel, sans lien avec les développeurs du jeu.
