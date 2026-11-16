# Intégration continue

Ce document décrit les workflows d'analyse du dépôt et la façon de lire leurs résultats.

## 1. Workflows

| Workflow | Fichier | Déclencheurs | Rôle |
| --- | --- | --- | --- |
| CodeQL | `.github/workflows/codeql.yml` | PR, push `master`, lundi 04:23, manuel | Analyse sémantique C/C++ (suite `security-extended`) |
| Static analysis | `.github/workflows/static-analysis.yml` | PR, push `master`, manuel | `/analyze` MSVC, cppcheck, rapport de warnings `/W4` |
| Build and release | `.github/workflows/build-release.yml` | PR, tag `v*`, manuel | Compilation Windows x64 + archive de release |
| Dependabot | `.github/dependabot.yml` | hebdomadaire | Mises à jour groupées des actions GitHub |

Les workflows s'exécutent sur `windows-2022` (build MSVC) ou `ubuntu-latest` (cppcheck). Le SDK LMU est remplacé par le header de secours `third_party/lmu_sdk` : aucune installation de Le Mans Ultimate n'est nécessaire.

Toutes les analyses sont **informatives** pour l'instant : elles ne bloquent pas une PR (pas de `--error-exitcode`, pas de `/WX`).

## 2. Où lire les résultats

- **Annotations inline** : directement dans l'onglet *Files changed* d'une PR, ou dans le résumé du run. Elles sont limitées aux 50 premiers findings par outil.
- **Résumé du run** : section *Summary* de chaque job (nombre de findings, tableau par code, 20 à 30 premiers résultats).
- **Artifacts** (conservés 14 jours) :
  - `msvc-analyze` : log de compilation avec `/analyze` ;
  - `cppcheck-report` : rapport texte complet ;
  - `msvc-build-log` : log du build `/W4` seul.
- **CodeQL** : onglet **Security → Code scanning** du dépôt (résultats sur `master` et sur les PR).

En ligne de commande :

```bash
gh run list --branch master
gh run watch <run-id>
gh run view <run-id> --log
```

## 3. Détails techniques

- `/analyze` est injecté via la variable d'environnement `CL`, pas via `CMAKE_CXX_FLAGS` : les flags par défaut de CMake (`/EHsc`, `/GR`, `/D_WINDOWS`…) restent intacts.
- `/analyze:external-` exclut `third_party/` de l'analyse ; ces dossiers sont déjà marqués `external` par `CMakeLists.txt`.
- CodeQL ne lance que des requêtes de sécurité (`security-extended`). La suite `security-and-quality` ajoutait des requêtes de maintenabilité qui signalaient massivement les headers vendored (`cpp/commented-out-code` ×112) ; cette couverture est assurée par `/analyze` et cppcheck. Le filtrage `paths-ignore` de CodeQL n'a pas cet effet pour le C/C++ : les headers inclus dans le build restent extraits.
- MSVC n'écrit aucun log SARIF exploitable quand on passe par MSBuild (`/analyze:log` et `:autolog:ext:sarif` restent sans effet). Les résultats sont donc extraits du log de compilation et publiés en annotations. Le SARIF reste couvert par CodeQL.
- Les warnings `/W4` proviennent des options déjà définies dans `CMakeLists.txt` ; le job dédié les compte sans `/analyze`, pour garder une baseline fidèle au build réel.

## 4. Findings connus (novembre 2026)

| Outil | Nombre | Détail |
| --- | --- | --- |
| CodeQL | 4 | 3 × `cpp/path-injection` (haute sévérité) : `src/core/paths.cpp:305`, `src/core/state.cpp:16`, `src/gui/main_gui.cpp:141` ; 1 × `cpp/non-https-url` dans `third_party/httplib.h` (vendored) |
| MSVC `/analyze` | 3 | `C4456` + `C6246` (`src/core/bridge.cpp:157`, shadowing de `now`), `C28251` (`src/gui/main_gui.cpp:1222`, `wWinMain` sans annotation) |
| cppcheck | 8 | 7 × `useStlAlgorithm`, 1 × `shadowVariable` (`src/core/bridge.cpp:157`) |

Les `cpp/path-injection` portent sur des chemins construits depuis l'API Win32 (dossiers SimPro, arguments CLI) : à revoir avant de considérer la base comme saine, même si le binaire tourne avec les droits de l'utilisateur.

`third_party/` n'est pas corrigé : ces dépendances sont suivies en amont. Le bruit éventuel y est neutralisé en amont (suite de requêtes, `/analyze:external-`, `-i third_party`).

## 5. Passer en mode bloquant

Après correction des findings :

1. cppcheck : ajouter `--error-exitcode=1` et retirer `|| true` dans `static-analysis.yml`.
2. MSVC : ajouter `/WX` et `/analyze:WX` (ou `/analyze:WX-` pour ne bloquer que sur les warnings du compilateur).
3. CodeQL : dans *Settings → Code security*, passer le seuil de merge à *Errors* ou *Errors and warnings*.
4. Vérifier qu'une PR volontairement fautive échoue bien, puis retirer le caractère informatif du tableau ci-dessus.

## 6. Dépannage

- **« Cannot build an overlay database » (CodeQL)** : annotation bénigne, liée aux builds C/C++ manuels sur les PR. Aucun impact sur l'analyse.
- **Artifact absent** : le step qui le produit a probablement été sauté (build en échec) ; consulter le log du job.
- **Relancer une analyse** : `gh workflow run "Static analysis"` ou `gh workflow run CodeQL`.
- **cppcheck ne trouve rien** : vérifier que `third_party/` et les dossiers de build sont bien exclus (`-i`), sinon les erreurs d'inclusion noient le rapport.

## 7. Dependabot

Les actions sont mises à jour chaque lundi, en une seule PR groupée (`ci(deps): …`). Dependabot ne lit la configuration que sur la branche par défaut : le fichier doit donc être présent sur `master`.
