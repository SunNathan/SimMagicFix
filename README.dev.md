# SimagicFix — documentation développeur

Ce document décrit la compilation, l'architecture interne et les procédures de validation. Pour l'utilisation grand public, voir [`README.md`](README.md).

## 1. Prérequis de compilation

- Windows 10/11 x64.
- Visual Studio 2022 avec la charge de travail « Développement Desktop en C++ » (composant vcpkg inclus).
- CMake ≥ 3.21.
- vcpkg (fourni avec Visual Studio, ou installation autonome). Le projet utilise le mode manifest avec le triplet `x64-windows-static`.

Les dépendances C++ (MinHook, nlohmann/json) sont déclarées dans `vcpkg.json` et installées automatiquement à la configuration. Aucune installation de Le Mans Ultimate n'est nécessaire pour compiler.

Pour la validation ABI locale uniquement, le header officiel du SDK LMU peut être fourni :

```bat
cmake --preset vs2022-x64 -DLMU_SDK_DIR="C:/Program Files (x86)/Steam/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
```

Cela ajoute la cible optionnelle `sdk_check`, qui affiche les tailles/offsets du header officiel pour comparaison avec `selftest`.

## 2. Compilation

Avec `build.bat`, `VCPKG_ROOT` est détecté automatiquement. Pour appeler CMake directement, définir `VCPKG_ROOT` :

```bat
set VCPKG_ROOT=C:\vcpkg
```

Puis :

```bat
cmake --preset vs2022-x64
cmake --build --preset release
```

Ou simplement :

```bat
build.bat
```

`build.bat` détecte `VCPKG_ROOT`, `VCPKG_INSTALLATION_ROOT`, `C:\vcpkg` puis le vcpkg intégré à Visual Studio.

Sorties :

- `build/Release/SimagicFix.exe` : application Win32 (installeur + tray).
- `build/Release/SimagicFixCLI.exe` : diagnostics.
- `build/Release/SimagicFixHook.dll` : DLL injectée dans SimPro.

## 3. Architecture

```
LMU_Data (mémoire partagée LMU)
        │  lecture seule
        ▼
TelemetryService  ──snapshot──►  SimProHookService
   (thread, 5 ms)                 (thread, 5 ms)
                                      │ injection + shared memory
                                      ▼
                            SimagicFixHook.dll (dans SimPro)
                                      │ hook de la copie game_data
                                      ▼
                            buffer game_data de SimPro
```

- `TelemetryService` (`src/core/telemetry.{h,cpp}`) : ouvre `LMU_Data`, copie uniquement le `TelemInfoV01` du joueur (1 888 octets) et publie un `LmuSnapshot`. Aucun serveur HTTP, aucun proxy JSON-RPC.
- `SimProHookService` (`src/core/simpro_hook.{h,cpp}`) : détecte la fenêtre `SimPro Manager`, injecte `SimagicFixHook.dll`, gère les redémarrages de SimPro et met à jour la mémoire partagée toutes les 5 ms.
- `SimagicFixHook.dll` (`src/hook/hook.cpp`) : MinHook sur la fonction de copie `game_data` de SimPro 2.1.8. Elle recopie les champs LMU dans le buffer de destination de SimPro.
- `SimagicFix.exe` (`src/gui/main_gui.cpp`) : interface Win32, tray, installation/désinstallation.
- `SimagicFixCLI.exe` (`src/cli/main_cli.cpp`) : diagnostics et tests.

### Mode unique

Le hook est le **seul** mode pris en charge. Si l'injection ou le hook échoue, SimagicFix n'écrit rien et n'effectue aucun repli vers une écriture externe : l'erreur est affichée et le patch reste inactif.

## 4. Détails du hook SimPro

Cible : SimPro Manager **2.1.8**.

| Élément | Valeur |
| --- | --- |
| RVA de la fonction de copie | `0x469c90` |
| Buffer destination 1 | `0x13215c0` |
| Buffer destination 2 | `0x1322240` |
| Buffer destination 3 | `0x1323638` |
| Signature du prologue | `48 89 5C 24 08 57 48 83 EC 20 8B 02 48 8B DA 89 01` |

Champs écrits dans le buffer SimPro :

| Offset | Champ |
| --- | --- |
| `+0x04` | engine map |
| `+0x70` | ABS |
| `+0x74` | TC |
| `+0x78` | TC Cut |
| `+0xF4` | ERS max |
| `+0xF8` | ERS stocké |
| `+0xFC` | ERS pourcentage |
| `+0x20C` | ARB avant |
| `+0x210` | ARB arrière |

Mémoire partagée :

- `Local\SimagicFixHook` : structure `HookSharedValues` (seqlock).
- `Local\SimagicFixHookReady` : événement signalant que la DLL est prête.

La DLL vérifie la signature du binaire SimPro avant d'installer le hook. Une version non reconnue renvoie `HookError::VersionMismatch`.

### Contraintes de performance

- Aucun `HIGH_PRIORITY_CLASS`, aucun `timeBeginPeriod(1)`, aucune priorité de thread élevée.
- Pas de scan mémoire des 4 Go de SimPro en mode hook : seuls les buffers connus sont utilisés.
- Copie de 1 888 octets au lieu de 196 356 octets pour la télémétrie.
- Boucles à 5 ms côté application, alignées sur la cadence de LMU.

## 5. Outils de diagnostic

```bat
SimagicFixCLI.exe selftest
SimagicFixCLI.exe probe
SimagicFixCLI.exe mock-shm 30
SimagicFixCLI.exe patch-memory 30
SimagicFixCLI.exe patch-memory 30 --mock
```

- `selftest` : tailles et offsets des structures LMU.
- `probe` : valeurs LMU en direct.
- `mock-shm` : crée un faux `LMU_Data` avec des valeurs connues (TC=5, ABS=3, MAP=7, SoC=64).
- `patch-memory` : lance la télémétrie et le hook pendant N secondes, puis affiche l'état (`installed`, `error`, valeurs).
- `patch-memory --mock` : mêmes tests sans LMU ni SimPro.

Autres commandes : `install [--no-autostart]`, `autostart --enable|--disable|--status`.

Procédure de validation complète : [`docs/VALIDATION.md`](docs/VALIDATION.md).

## 6. Analyse statique et CI

| Workflow | Fichier | Rôle |
| --- | --- | --- |
| CodeQL | `.github/workflows/codeql.yml` | Analyse sémantique C/C++ (`security-extended`) |
| Static analysis | `.github/workflows/static-analysis.yml` | `/analyze` MSVC, cppcheck, warnings `/W4` |
| Build and release | `.github/workflows/build-release.yml` | Compilation Windows x64 + archive de release |

Détails et lecture des résultats : [`docs/CI.md`](docs/CI.md).

Commandes locales :

```bat
:: cppcheck
cppcheck --std=c++20 --enable=warning,performance,portability,style ^
  --suppress=missingIncludeSystem -i build src

:: MSVC /analyze (dans un build séparé)
set CL=/analyze /analyze:external-
cmake -S . -B build-analyze -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build-analyze --config Release
```

Le code source est analysé avec zéro finding cppcheck et zéro warning MSVC `/analyze`.

## 7. Sécurité

- Lecture seule de `LMU_Data`.
- Injection dans **SimPro uniquement**, jamais dans LMU.
- Aucun fichier SimPro modifié.
- Les chemins de base (`%LOCALAPPDATA%`, `Program Files`, menu Démarrer) proviennent de `SHGetKnownFolderPath`.
- Les chemins issus du registre et des fichiers Steam sont validés : absolus, locaux, sans `..` ni chemin UNC.

## 8. Release

1. Mettre à jour la version dans `CMakeLists.txt`, `src/core/installer.cpp`, `src/gui/app.rc` et `vcpkg.json`.
2. Compiler en Release.
3. Vérifier `cppcheck` et MSVC `/analyze`.
4. Tester `patch-memory 30 --mock` puis avec SimPro et LMU.
5. Créer le tag `v<version>` : le workflow `build-release.yml` produit l'archive.

L'archive de release contient exactement :

- `SimagicFix.exe`
- `SimagicFixHook.dll`
- `LICENSE`
- `README.md`

`README.md` embarque les notices de licences tierces (MinHook, nlohmann/json) afin que l'archive binaire reste conforme sans fichier supplémentaire. Le fichier `THIRD_PARTY_NOTICES.md` reste dans le dépôt pour la documentation développeur.

## 9. Licence et dépendances

MIT (voir `LICENSE`). Dépendances gérées par vcpkg : MinHook (BSD 2-Clause), `nlohmann/json` (MIT). Les types LMU sont définis dans `src/core/lmu_types.h` ; le header officiel du jeu n'est ni requis ni redistribué. Détails dans `THIRD_PARTY_NOTICES.md`.

Pour régénérer les icônes :

```bat
python -m pip install -r requirements-dev.txt
python tools/make_icon.py
```
