# SimagicFix

![Icône SimagicFix](resources/icon-preview.png)

Correctif local pour la télémétrie **Le Mans Ultimate** sur **SimPro Manager 2.x** (bases Alpha) avec volant **Simagic FX Pro**.

Le correctif relie les valeurs **TC**, **TC Cut**, **ABS**, **Motor Map** et **SoC** aux dashboards SimPro, sans modifier les layouts : seules les expressions de valeur sont remplacées.

## Pourquoi ce correctif

- LMU expose ces valeurs dans sa mémoire partagée native `LMU_Data` (`TelemInfoV01` : `mTC`, `mTCCut`, `mABS`, `mMotorMap`, `mSoC`, etc.).
- SimPro Manager 2.1.8 (branche des bases Alpha) lit encore l'ancien plugin rFactor 2, qui ne contient pas ces champs. Les améliorations LMU (SOC/ABS/TC/ARB) sont dans SimPro 3.x, réservé aux bases EVO/Zeus.
- Les dashboards officiels contiennent aussi des valeurs figées (par exemple `TC 1 = 2`, `soc = 94`, `MAP = nombre de tours`).

SimagicFix fait le pont entre `LMU_Data` et les dashboards, puis corrige uniquement les valeurs codées en dur.

## Fonctionnalités

- Bridge local en lecture seule vers `LMU_Data`.
- Proxy JSON-RPC : les dashboards continuent d'utiliser SimPro pour le reste (vitesse, RPM, pneus, chronos...).
- Patch **valeurs uniquement** : aucun widget ajouté, supprimé ou déplacé.
- Patch de tous les dashboards compatibles détectés.
- Sauvegarde automatique avant patch.
- Réparation après une mise à jour SimPro.
- Restauration des dashboards d'origine.
- Démarrage automatique avec Windows, **activable/désactivable** à tout moment.
- Désinstallation propre.
- Mode démo / outil de diagnostic.

## Prérequis

- Windows 10/11 x64.
- SimPro Manager 2.x (base Alpha).
- Le Mans Ultimate 1.3 ou plus récent (mémoire partagée native).
- `tar.exe` (fourni avec Windows 10/11) pour manipuler les archives `.dash`.
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
2. Vérifier la liste des dashboards compatibles.
3. Cocher ou décocher « Lancer SimagicFix au démarrage de Windows ».
4. Cliquer sur **Installer le correctif**.
5. Si SimPro est ouvert, l'installeur propose de le fermer puis le relance.
6. Le bridge démarre et l'icône apparaît dans la zone de notification.

Ensuite :

- **Tester les valeurs** : affiche TC / TC Cut / ABS / MAP / SoC en direct (ou en mode démo).
- **Réparer** : rescane et réapplique les patchs après une mise à jour SimPro.
- **Restaurer les dashboards** : remet les fichiers d'origine, sans désinstaller le correctif.
- **Désinstaller** : restaure les dashboards, retire l'auto-démarrage et le raccourci.
- **Démarrage automatique** : case à cocher dans la fenêtre ou entrée du menu tray.

Si l'auto-démarrage est désactivé, le correctif n'est actif que lorsque l'application est lancée (menu Démarrer ou raccourci).

## Outils de diagnostic

```bat
SimagicFixCLI.exe selftest
SimagicFixCLI.exe probe
SimagicFixCLI.exe scan
SimagicFixCLI.exe patch-dash --all
SimagicFixCLI.exe restore-dash
SimagicFixCLI.exe mock-shm 30
SimagicFixCLI.exe run --mock
```

- `selftest` : tailles et offsets des structures LMU.
- `probe` : valeurs LMU en direct.
- `scan` : dashboards compatibles et état du patch.
- `mock-shm` : crée un faux `LMU_Data` avec des valeurs connues (TC=5, ABS=3, MAP=7, SoC=64) pour valider la lecture sans LMU.
- `run --mock` : bridge avec valeurs simulées, utile pour tester un dashboard dans un navigateur.

## Icône

L'icône de l'application est une création communautaire : un “S” blanc italique sur un cercle rouge, inspiré des couleurs de Simagic. **Aucun logo officiel Simagic n'est embarqué dans le projet.** Simagic est une marque de Simagic ; ce projet est non officiel et n'est pas affilié à Simagic.

L'icône est générée par `tools/make_icon.py` (Pillow, uniquement au build) et versionnée dans `src/gui/app.ico` / `src/gui/tray.ico`.

## Sécurité et anti-triche

- Lecture **read-only** de la mémoire partagée officielle de LMU.
- Aucune écriture dans le processus du jeu, aucun fichier LMU modifié.
- Aucune injection, aucun hook.
- Les dashboards SimPro sont sauvegardés avant modification.
- Les sauvegardes se trouvent dans `%LOCALAPPDATA%\SimagicFix\backups`.

## Limites

- ABS n'est affiché que sur les dashboards qui possèdent déjà une case ABS. Le correctif ne modifie pas les layouts.
- Une mise à jour SimPro peut écraser les dashboards : utiliser **Réparer**.
- Une mise à jour LMU peut modifier la structure de `LMU_Data` : `selftest`/`probe` permettent de le détecter ; recompiler avec le header SDK mis à jour.
- Le correctif ne fonctionne que si l'application (le bridge) est lancée.
- L'exécutable n'est pas signé : Windows SmartScreen peut afficher un avertissement.

## Architecture

- C++20, CMake, Win32 natif.
- `cpp-httplib` (serveur/client HTTP local).
- `nlohmann/json`.
- Header SDK officiel LMU inclus depuis l'installation du jeu (non redistribué).
- `simagicfix_core` : lecteur LMU, bridge, patcher, installeur.
- `SimagicFix.exe` : interface Win32.
- `SimagicFixCLI.exe` : diagnostics.

Voir `THIRD_PARTY_NOTICES.md` pour les licences des dépendances.
