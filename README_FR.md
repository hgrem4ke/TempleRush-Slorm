# TempleRush pour The Slormancer

*English version: [README.md](README.md)*

Deux choses pour **Le Temple du Slorm** : terminer un étage vite débloque plus d'étages, et une nouvelle amélioration du Slorm Juteux retire des pièces à chaque étage.

## Bonus de rapidité

Plus tu termines un étage vite, plus tu débloques d'étages.

| Temps pour terminer l'étage | Étages débloqués |
|-----------------------------|------------------|
| Plus de 2 minutes | 1 (la progression normale du jeu) |
| Moins de 2 minutes | 3 (+2) |
| Moins de 90 secondes | 4 (+3) |
| Moins de 1 minute | 5 (+4) |
| Moins de 30 secondes | 6 (+5) |

Exemple : si l'étage 61 est ton plus haut étage terminé et que tu termines l'étage 62 en moins de
2 minutes, c'est l'étage 65 qui devient disponible au lieu du 63.

Le bonus ne s'applique que la première fois que tu termines un étage (nouveau record). Rejouer un
étage déjà terminé ne donne aucun bonus. L'exécutable du jeu n'est jamais modifié.

## Pièces par étage du Temple (amélioration du Slorm Juteux)

Parle au Slorm Juteux : la boîte à cadre doré **Temple rush**, sur le carré noir en haut à gauche du portrait
du PNJ, ouvre la page de TempleRush, dans le style du panneau de SlormReslormitizer (le cadre, le sélecteur,
les boutons et les polices du jeu). Elle liste les améliorations du mod (d'autres pourront être ajoutées) ;
clique sur une ligne pour la sélectionner, puis sur **Acheter**. La page a son propre sélecteur de langue
(anglais / français).

**Pièces par étage du Temple** retire des pièces à chaque étage du Temple du Slorm :

- 10 rangs : chaque rang retire 1 pièce, donc le rang 10 (le maximum) retire 10 pièces ;
- un étage garde toujours au moins 3 pièces ;
- chaque rang a son prix, en Slorm purs (210 297 au total) :

| Rang | Prix (Slorm purs) | Coût cumulé |
|--:|--:|--:|
| 1 | 921 | 921 |
| 2 | 1 519 | 2 440 |
| 3 | 2 506 | 4 946 |
| 4 | 4 134 | 9 080 |
| 5 | 6 820 | 15 900 |
| 6 | 11 253 | 27 153 |
| 7 | 18 566 | 45 719 |
| 8 | 30 634 | 76 353 |
| 9 | 50 545 | 126 898 |
| 10 | 83 399 | 210 297 |

**Remboursement.** La même page a deux boutons de plus : **Rembourser** rend le dernier rang (ce que tu l'as payé) et
**Tout rembourser** (deux clics pour confirmer) retire tous les rangs et rend tous les Slorm purs dépensés.
Le prix payé pour chaque rang est enregistré : un changement de prix dans une version future ne te fait rien perdre.
Descendre d'un rang rend la pièce correspondante, à partir de l'étage suivant.

**Pendant un étage**, une ligne sous le minuteur indique ce que l'amélioration a fait, tant que tu le joues :
« -2 pièces : 4 au lieu de 6 », « … (minimum atteint) » ou « Aucune pièce retirée : déjà au minimum ».

Le sélecteur **Langue** en haut à gauche de la page change sa langue (il est mémorisé ; la première fois, il
suit la langue de Windows). Échap, le bouton X ou un clic en dehors de la page la ferme.

Le niveau est enregistré **par emplacement de sauvegarde** dans `TempleRush_rooms.txt`, dans le dossier du jeu
(pas dans la sauvegarde du jeu). Copie ce fichier avec ta sauvegarde si tu changes de PC.

## Minuteur à l'écran

Pendant un étage du Temple, un cadre en forme de titre d'objet apparaît en haut à droite de l'écran, sous la ligne
« Trouver l'Autel » du jeu et sa barre de Slorm à dépenser. Il affiche **ÉTAGE SUIVANT +N** et le temps qu'il te
reste pour ce bonus. La bordure et le texte changent de couleur avec le bonus, comme la rareté d'un objet : gris-bleu
pour +2, or pour +3, orange pour +4 et violet (Néant) pour +5. Le décompte démarre à l'apparition des premiers monstres
de l'étage : avant, le cadre affiche le temps complet, figé.

Les monstres apparaissent pièce par pièce : le décompte continue donc entre deux pièces. Dans la **dernière pièce**
(celle de l'autel), dès que le dernier monstre est mort, le décompte **se fige** : « OK » s'ajoute et le bonus atteint
est gardé, donc marcher jusqu'à l'autel et ramasser le Slorm ne te coûte rien. Si des monstres réapparaissent, il repart.

| Cadre | Signification |
|-------|---------------|
| `ÉTAGE SUIVANT +5  0:23` (violet) | 23 secondes pour terminer l'étage et gagner +5 étages |
| `ÉTAGE SUIVANT +2  1:12` (gris-bleu) | 1 min 12 s pour terminer l'étage et gagner +2 étages |
| `ÉTAGE SUIVANT +0  0:00` (gris) | trop tard, pas de bonus (disparaît au bout de 5 secondes) |
| `ÉTAGE SUIVANT +4  0:05 OK` (orange) | dernière pièce terminée : le décompte est figé et le bonus est gardé |
| `ÉTAGE SUIVANT +3` | après la fin de l'étage : le bonus obtenu (affiché 6 secondes) |

Il ne s'affiche que dans Le Temple du Slorm, pas dans le Champ de Bataille ni dans la Grande Forge.

**Étages de boss.** Le jeu a un boss tous les 50 étages (50, 100 et 150) et lance une partie sur ton étage le plus haut.
Un bonus ne te fait jamais passer au-dessus d'un étage de boss : il s'arrête une marche avant, pour que tu puisses
jouer le boss et obtenir son artefact.

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

Pour désinstaller, supprime `TempleRush.dll` (et `TempleRush_rooms.txt` pour oublier le niveau de l'amélioration).
Les étages déjà débloqués le restent.

## Réglages

Les temps (120, 90, 60 et 30 s), les bonus (+2, +3, +4 et +5) et les prix de l'amélioration (`kPriceBase`) sont fixés
dans le code. Pour les changer, modifie les constantes de `source/ModuleMain.cpp` puis recompile. Seuls le niveau
de l'amélioration et la langue de la page sont enregistrés, dans `TempleRush_rooms.txt`.

Le chrono compte au maximum 0,25 s par image : une pause compte très peu, mais ne l'arrête pas
complètement.

## Compilation

Visual Studio 2022 ou plus récent, avec « Développement Desktop en C++ ». Ouvre `TempleRush.vcxproj`
et compile en **Release / x64**. Les en-têtes Aurie et YYToolkit v5.0.0c sont dans `include/`.

## Historique des versions

### 1.3.1
- Le bouton d'ouverture est maintenant une boîte à cadre doré **Temple rush** (le cadre et la police du jeu) sur le portrait du PNJ, centrée dessus.
- La page est plus compacte : les Slorm possédés et la description de l'amélioration sont sur un fond rouge sombre, et l'icône du Slorm pur suit le prix et les remboursements.
- Le minuteur est maintenant un cadre en forme de titre d'objet, avec « ÉTAGE SUIVANT +N » et le temps. La bordure et le texte changent de couleur avec le bonus : gris-bleu +2, or +3, orange +4, violet +5. Il se place sous la barre de Slorm à dépenser du jeu.
- Le message sur les pièces retirées reste affiché pendant l'étage (il disparaissait au bout de 8 secondes).
- Le bonus de temps ne te fait plus passer au-dessus d'un étage de boss (50, 100, 150) : tu arrives sur l'étage du boss, pour pouvoir le jouer.

### 1.3.0
- Nouvelle page pour l'amélioration, dans le style du panneau de SlormReslormitizer (un mod de wistb) : le cadre, le sélecteur, les boutons et les polices du jeu. Le bouton d'ouverture est maintenant son étoile Slormite, grise (bleue quand la page est ouverte), sur le portrait du PNJ, à la place du losange M.
- « Pièces par étage du Temple » a maintenant 10 rangs d'une pièce chacun (jusqu'à -10 pièces) avec de nouveaux prix (210 297 Slorm purs au total), au lieu de 50 niveaux de 0,1 pièce.
- Rien de ce que tu as investi n'est perdu : le prix payé pour chaque rang est enregistré et remboursé tel quel, et **Tout rembourser** marche même au rang 0. Les anciens niveaux sont convertis (les mêmes pièces entières restent retirées), et les niveaux qui ne forment pas un rang entier sont remboursés automatiquement, une fois.
- La destination du portail après un record avec bonus était déjà corrigée en 1.2.4.

### 1.2.4
- Portail corrigé : après un nouveau record avec un bonus de temps, il t'envoie à l'étage terminé + 1 + le bonus (il t'envoyait seulement à l'étage suivant, surtout sur le premier étage après le lancement du jeu).

### 1.2.3
- L'icône « M » qui ouvre la page de l'amélioration est déplacée en bas à gauche de la fenêtre, près du bouton « ? ».

### 1.2.2
- Le minuteur est déplacé sous la ligne « Trouver l'Autel » du jeu (en haut à droite) et s'adapte à la résolution.
- Le décompte se fige dans la dernière pièce (celle de l'autel) dès que le dernier monstre est mort, et le bonus est gardé.

### 1.2.1
- Boutons de remboursement sur la page de l'amélioration : **Rembourser** (dernier niveau) et **Tout rembourser** (demande un second clic).
- En entrant dans un étage, un cadre indique combien de pièces il a et combien il en aurait eu sans l'amélioration.
- Prix de l'amélioration abaissé à 50 x niveau (il était de 500 x niveau en 1.2.0).

### 1.2.0
- Nouvelle amélioration du Slorm Juteux « Pièces par étage du Temple » : un losange **M** dans la fenêtre « Personnage »
  ouvre une page où tu dépenses des Slorm purs pour retirer jusqu'à 5 pièces à chaque étage du Temple (niveau enregistré par sauvegarde).
- Nouveaux paliers de bonus : moins de 2 minutes +2, moins de 90 secondes +3, moins de 1 minute +4, moins de 30 secondes +5.
- Sélecteur anglais / français sur la page.

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
