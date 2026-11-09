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

## Minuteur à l'écran

Pendant un étage du Temple, un petit cadre en haut de l'écran montre le bonus que tu peux encore
obtenir et le temps qu'il te reste, avec une barre qui se vide. Le décompte démarre à l'apparition
des premiers monstres de l'étage : avant, le cadre affiche le temps complet, figé.


| Cadre | Signification |
|-------|---------------|
| `+5  0:23` (or) | 23 secondes pour terminer l'étage et gagner +5 étages |
| `+1  1:12` (bleu) | 1 min 12 s pour terminer l'étage et gagner +1 étage |
| `+0  0:00` (gris) | trop tard, pas de bonus (disparaît au bout de 5 secondes) |
| `+1` (vert) | après la fin de l'étage : le bonus obtenu (affiché 6 secondes) |

Il ne s'affiche que dans Le Temple du Slorm, pas dans le Champ de Bataille ni dans la Grande Forge.

## Fonctionnement

Le décompte démarre à l'apparition des premiers monstres de l'étage. Quand le jeu termine l'étage, il enregistre le plus haut étage
terminé (`slorm_temple_data` dans le jeu, `temple_data` dans le fichier de sauvegarde). TempleRush
ajoute alors le bonus à cette valeur, juste avant la sauvegarde.

Le texte « Étage le plus élevé » en haut du menu du Temple est le compteur propre au jeu : il ne
comprend pas le bonus. Les étages que tu peux choisir, eux, le comprennent.

Le minuteur est dessiné via un script d'interface du jeu. S'il ne peut pas être accroché, TempleRush
fonctionne quand même, sans minuteur (le journal le signale).

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

## Historique des versions

### 1.1.0
- Minuteur à l'écran : affiche le bonus encore possible et le temps restant, avec une barre.
- Le décompte démarre maintenant à l'apparition des premiers monstres de l'étage, et non quand tu entres dans l'étage.
- Après la fin de l'étage, le bonus obtenu s'affiche pendant 6 secondes.

### 1.0.0
- Étage du Temple du Slorm terminé en moins de 2 minutes : +1 étage débloqué. En moins de 30 secondes : +5 étages.
- Le bonus ne s'applique qu'à un nouveau record d'étage.

## Licence

AGPL-3.0, voir [LICENSE](LICENSE). Le dossier `include/` contient les en-têtes Aurie et YYToolkit
(AGPL-3.0, AurieFramework).

Mod non officiel, sans lien avec les développeurs du jeu.
