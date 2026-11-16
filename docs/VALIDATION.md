# Validation avec Le Mans Ultimate

Cette procédure sert à confirmer les valeurs réelles de LMU et à vérifier le hook SimPro. Elle ne modifie rien dans le jeu.

## 1. Vérifier la structure

```bat
SimagicFixCLI.exe selftest
```

Comparer les tailles/offsets avec ceux du header installé :

`...\Le Mans Ultimate\Support\SharedMemoryInterface\InternalsPlugin.hpp`

Valeurs de référence sur la build d'octobre 2026 :

- `sizeof(TelemInfoV01)` = 1888
- `offsetof(TelemInfoV01, mTC)` = 750
- `offsetof(TelemInfoV01, mABS)` = 756
- `offsetof(TelemInfoV01, mMotorMap)` = 758
- `offsetof(TelemInfoV01, mSoC)` = 772
- `sizeof(SharedMemoryObjectOut)` = 324824
- `offsetof(SharedMemoryObjectOut, telemetry)` = 128464

## 2. Lancer LMU

- Lancer Le Mans Ultimate.
- Entrer dans une session **practice** (pas le menu).
- Prendre une Hypercar (ex. Ferrari 499P) puis une GT3.
- Vérifier que les plugins sont activés dans les options du jeu.

## 3. Lire les valeurs

Dans un terminal :

```bat
SimagicFixCLI.exe probe
```

Le probe affiche :

- TC / TC max
- TC Cut / TC Cut max
- TC Slip / TC Slip max
- ABS / ABS max
- Motor Map / Motor Map max
- Migration, ARB avant/arrière
- SoC, batterie, énergie virtuelle, regen
- Températures, carburant

## 4. Comparer avec le MFD

Dans la voiture, ouvrir le MFD électronique et modifier :

- TC
- TC Cut
- ABS (GT3)
- Motor Map

Relancer `probe` après chaque changement. Les valeurs doivent suivre.

## 5. Vérifier le hook

1. Lancer **SimPro Manager 2.1.8**.
2. Lancer **SimagicFix.exe** (installé) et cocher **« Activer le patch memoire SimPro (hook) »**.
3. La ligne **Hook SimPro** doit afficher `actif`.

Test en ligne de commande :

```bat
SimagicFixCLI.exe patch-memory 30
```

La sortie doit indiquer `installed=yes`, `error=""`, puis les valeurs LMU courantes.

Test sans LMU ni SimPro :

```bat
SimagicFixCLI.exe patch-memory 10 --mock
```

La sortie doit afficher `lmu=Mock Hypercar` et des valeurs qui évoluent.

## 6. Vérifier le volant

- Ouvrir SimPro Manager.
- Activer **« Activer le patch memoire SimPro (hook) »** dans SimagicFix.
- Vérifier que TC / TC Cut / MAP / SoC / ARB avant / ARB arrière changent quand on agit dans le MFD.

Si le volant affiche 0 :

- Vérifier que la ligne **Hook SimPro** affiche `actif`.
- Vérifier que SimPro est bien en version **2.1.8** (les offsets et l'adresse du hook sont spécifiques à cette version).
- Lancer `SimagicFixCLI.exe patch-memory 30` pour voir `installed` et l'erreur éventuelle.
- Consulter `%LOCALAPPDATA%\SimagicFix\logs\app.log`.

Aucun repli vers une écriture externe n'existe : si le hook échoue, le patch reste inactif et l'erreur est affichée.

## 7. Points à confirmer

- Unité de `mSoC` : le code normalise `<= 1.0` en pourcentage. Si LMU renvoie déjà 0-100, la valeur reste telle quelle.
- Mapping `mTC` / `mTCCut` selon les voitures (certaines voitures ont des TC liés).
- Voitures sans ABS : la valeur peut rester à 0, c'est normal.
- Voitures sans hybrid : SoC peut rester à 0.

## 8. En cas de changement de build LMU

Si `probe` affiche des valeurs incohérentes ou si `selftest` ne correspond plus :

1. Vérifier le header `Support\SharedMemoryInterface\InternalsPlugin.hpp`.
2. Comparer les champs avec `src/core/lmu_reader.cpp`.
3. Recompiler avec le header mis à jour.

## 9. En cas de changement de build SimPro

Le hook dépend de l'adresse de la fonction de copie `game_data` et des offsets du buffer SimPro. Après une mise à jour de SimPro :

1. Vérifier la signature du prologue (`src/hook/hook.cpp`).
2. Réanalyser le binaire SimPro et mettre à jour les RVA/offsets.
3. Rejouer `patch-memory 30` avec SimPro lancé.
