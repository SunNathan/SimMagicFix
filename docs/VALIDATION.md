# Validation avec Le Mans Ultimate

Cette procédure sert à confirmer les valeurs réelles de LMU et à ajuster le mapping si nécessaire. Elle ne modifie rien dans le jeu.

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

## 5. Vérifier le bridge

Le bridge tourne si l'application est lancée. Tester :

```bat
curl http://127.0.0.1:56799/status
```

ou dans un navigateur :

`http://127.0.0.1:56799/status`

Le JSON doit indiquer `"lmu_available": true` et les valeurs courantes.

## 6. Vérifier le volant

- Ouvrir SimPro Manager.
- Activer **« Patch memoire SimPro (experimental) »** dans SimagicFix.
- Choisir **« Hook SimPro (experimental) »** pour le mode déterministe.
- Vérifier que TC / TC Cut / MAP / SoC / ARB avant / ARB arrière changent quand on agit dans le MFD.

Si le volant affiche 0 :

- Vérifier que le bridge tourne (`/status`).
- Vérifier que le patch mémoire est actif (statut « Patch memoire : actif (hook) »).
- Lancer `SimagicFixCLI.exe patch-memory 30 --hook` pour voir les valeurs et les erreurs.
- Vérifier que SimPro n'a pas été mis à jour entre-temps (les offsets et l'adresse du hook sont spécifiques à 2.1.8).

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
