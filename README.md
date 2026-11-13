# SimagicFix

![Icône SimagicFix](resources/icon-preview.png)

Correctif local pour la télémétrie **Le Mans Ultimate** sur **SimPro Manager 2.x** (bases Alpha) avec volant **Simagic FX Pro**.

Le rendu natif du volant SimPro ne lit ni les fichiers `.dash` ni le RPC JSON : il utilise un buffer `game_data` interne. SimagicFix lit les valeurs **TC**, **TC Cut**, **ABS**, **Motor Map** et **SoC** dans `LMU_Data`, puis les écrit directement dans ce buffer mémoire. Aucun fichier de dashboard n'est modifié.

## Pourquoi ce correctif

- LMU expose ces valeurs dans sa mémoire partagée native `LMU_Data` (`TelemInfoV01` : `mTC`, `mTCCut`, `mABS`, `mMotorMap`, `mSoC`, etc.).
- SimPro Manager 2.1.8 (branche des bases Alpha) lit encore l'ancien plugin rFactor 2, qui ne contient pas ces champs. Les améliorations LMU (SOC/ABS/TC/ARB) sont dans SimPro 3.x, réservé aux bases EVO/Zeus.
- Le dashboard natif du volant affiche donc des valeurs absentes ou figées.

SimagicFix fait le pont entre `LMU_Data` et le buffer `game_data` de SimPro, sans modifier les layouts ni les fichiers SimPro.

## Fonctionnalités

- Bridge local en lecture seule vers `LMU_Data`.
- Patch mémoire du buffer `game_data` natif de SimPro : TC, TC Cut, ABS, Motor Map et SoC.
- Aucun dashboard modifié, aucune sauvegarde de fichier `.dash`.
- Démarrage automatique avec Windows, **activable/désactivable** à tout moment.
- Désinstallation propre.
- Fenêtre **Tester** et mode démo pour vérifier les valeurs lues.
- Outil de diagnostic en ligne de commande.

## Prérequis

- Windows 10/11 x64.
- SimPro Manager 2.1.8 (base Alpha).
- Le Mans Ultimate 1.3 ou plus récent (mémoire partagée native).
- Aucun droit administrateur nécessaire.

## Compilation

Le projet utilise CMake et MSVC 2022.

```bat
cmake --preset vs2022-x64
cmake --build --preset release
```

Ou simplement :

```bat
build.bat
```

Le SDK LMU est détecté automatiquement dans les emplacements Steam habituels. Sinon :

```bat
cmake --preset vs2022-x64 -DLMU_SDK_DIR="C:/Program Files (x86)/Steam/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
```

Sorties :

- `build/Release/SimagicFix.exe` : application Win32 (installeur + tray).
- `build/Release/SimagicFixCLI.exe` : diagnostics.

## Utilisation

1. Lancer `SimagicFix.exe`.
2. Cocher ou décocher « Lancer SimagicFix au démarrage de Windows ».
3. Cliquer sur **Installer SimagicFix**.
4. Le bridge démarre et l'icône apparaît dans la zone de notification.
5. Cocher **« Patch memoire SimPro (experimental) »** pour activer le patch du volant.

Ensuite :

- **Patch memoire SimPro** : case à cocher dans la fenêtre ; l'état actif / en attente / arrêté est affiché au-dessus.
- **Tester** : affiche TC / TC Cut / ABS / MAP / SoC en direct (ou en mode démo).
- **Ouvrir les logs** : ouvre `%LOCALAPPDATA%\SimagicFix\logs`.
- **Désinstaller** : arrête le bridge et le patch, retire l'auto-démarrage et le raccourci.
- **Démarrage automatique** : case à cocher dans la fenêtre ou entrée du menu tray.

L'état est mémorisé dans `state.json` et retrouvé au prochain lancement.

Si l'auto-démarrage est désactivé, le correctif n'est actif que lorsque l'application est lancée (menu Démarrer ou raccourci).

## Patch mémoire SimPro (expérimental)

SimPro rend le dashboard du volant nativement. Pour que TC, TC Cut, ABS, Motor Map et SoC apparaissent sur le volant, SimagicFix écrit ces valeurs directement dans le buffer `game_data` du processus SimPro.

- Activer la case **« Patch memoire SimPro (experimental) »** dans la fenêtre principale.
- Le patch démarre avec le bridge et s'arrête avec lui.
- Il ne modifie aucun fichier SimPro et ne touche pas au processus du jeu.
- Il utilise un cœur CPU pendant une session LMU active.
- Les offsets sont spécifiques à SimPro 2.1.8 ; une mise à jour de SimPro peut les invalider.

Diagnostic :

```bat
SimagicFixCLI.exe patch-memory 30
```

## Outils de diagnostic

```bat
SimagicFixCLI.exe selftest
SimagicFixCLI.exe probe
SimagicFixCLI.exe mock-shm 30
SimagicFixCLI.exe patch-memory 30
SimagicFixCLI.exe run --mock
```

- `selftest` : tailles et offsets des structures LMU.
- `probe` : valeurs LMU en direct.
- `mock-shm` : crée un faux `LMU_Data` avec des valeurs connues (TC=5, ABS=3, MAP=7, SoC=64) pour valider la lecture sans LMU.
- `patch-memory` : lance le bridge et le patch mémoire pendant N secondes, puis affiche l'état.
- `run --mock` : bridge avec valeurs simulées.

## Icône

L'icône de l'application est une création communautaire : un “S” blanc italique sur un cercle rouge, inspiré des couleurs de Simagic. **Aucun logo officiel Simagic n'est embarqué dans le projet.** Simagic est une marque de Simagic ; ce projet est non officiel et n'est pas affilié à Simagic.

L'icône est générée par `tools/make_icon.py` (Pillow, uniquement au build) et versionnée dans `src/gui/app.ico` / `src/gui/tray.ico`.

## Sécurité et anti-triche

- Lecture **read-only** de la mémoire partagée officielle de LMU.
- Aucune écriture dans le processus du jeu, aucun fichier LMU modifié.
- Le patch mémoire écrit uniquement dans le processus **SimPro** (buffer `game_data`), jamais dans LMU.
- Aucune injection de DLL, aucun hook.
- Aucun fichier SimPro n'est modifié.

## Limites

- Le patch mémoire est spécifique à SimPro 2.1.8 ; il peut cesser de fonctionner après une mise à jour de SimPro.
- Le patch mémoire utilise un cœur CPU pendant une session LMU active.
- Une mise à jour LMU peut modifier la structure de `LMU_Data` : `selftest`/`probe` permettent de le détecter ; recompiler avec le header SDK mis à jour.
- Le correctif ne fonctionne que si l'application (le bridge) est lancée.
- L'exécutable n'est pas signé : Windows SmartScreen peut afficher un avertissement.

## Licence

MIT. Voir `LICENSE`. L'utilisation commerciale est autorisée par la licence MIT.

## Architecture

- C++20, CMake, Win32 natif.
- `cpp-httplib` (serveur/client HTTP local).
- `nlohmann/json`.
- Header SDK officiel LMU inclus depuis l'installation du jeu (non redistribué).
- `simagicfix_core` : lecteur LMU, bridge, patch mémoire, installeur.
- `SimagicFix.exe` : interface Win32.
- `SimagicFixCLI.exe` : diagnostics.

Voir `THIRD_PARTY_NOTICES.md` pour les licences des dépendances.
