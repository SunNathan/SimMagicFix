# Intégration continue

Ce document décrit les workflows d'analyse du dépôt et la façon de lire leurs résultats.

## 1. Workflows

| Workflow | Fichier | Déclencheurs | Rôle |
| --- | --- | --- | --- |
| CodeQL | `.github/workflows/codeql.yml` | PR, push `master`, lundi 04:23, manuel | Analyse sémantique C/C++ (suite `security-extended`) |
| Static analysis | `.github/workflows/static-analysis.yml` | PR, push `master`, manuel | `/analyze` MSVC, cppcheck, rapport de warnings `/W4` |
| Build and release | `.github/workflows/build-release.yml` | PR, tag `v*`, manuel | Compilation Windows x64 + archive de release |
| Dependabot | `.github/dependabot.yml` | hebdomadaire | Mises à jour groupées des actions GitHub |

Les workflows s'exécutent sur `windows-2022` (build MSVC) ou `ubuntu-latest` (cppcheck). Le SDK LMU n'est plus requis : les types de mémoire partagée sont définis dans `src/core/lmu_types.h`. Les dépendances C++ (MinHook, nlohmann/json) sont installées par vcpkg en mode manifest.

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

- `/analyze` est injecté via la variable d'environnement `CL`, pas via `CMAKE_CXX_FLAGS` : les flags par défaut de CMake (`/EHsc`, `/GR`, `/D_WINDOWS`…) restent intacts. Il est appliqué au step de build uniquement, pour que vcpkg compile les dépendances sans `/analyze`.
- `/analyze:external-` exclut les headers des dépendances vcpkg de l'analyse ; le toolchain les marque déjà comme externes.
- vcpkg utilise le triplet `x64-windows-static` (CRT statique, cohérent avec `/MT`) et un cache binaire `vcpkg-cache` restauré par `actions/cache`.
- CodeQL ne lance que des requêtes de sécurité (`security-extended`). La suite `security-and-quality` ajoutait des requêtes de maintenabilité qui signalaient massivement les headers vendored (`cpp/commented-out-code` ×112) ; cette couverture est assurée par `/analyze` et cppcheck.
- MSVC n'écrit aucun log SARIF exploitable quand on passe par MSBuild (`/analyze:log` et `:autolog:ext:sarif` restent sans effet). Les résultats sont donc extraits du log de compilation et publiés en annotations. Le SARIF reste couvert par CodeQL.
- Les warnings `/W4` proviennent des options déjà définies dans `CMakeLists.txt` ; le job dédié les compte sans `/analyze`, pour garder une baseline fidèle au build réel.

## 4. État des findings (novembre 2026)

Après la migration vers le hook SimPro comme mode unique, le code source est analysé sans finding :

| Outil | Findings `src/` | Détail |
| --- | --- | --- |
| CodeQL | 0 | Les `cpp/path-injection` ont été corrigés : chemins de base via `SHGetKnownFolderPath`, chemins du registre et de Steam validés (absolus, locaux, sans `..` ni UNC) |
| MSVC `/analyze` | 0 | Le shadowing de `bridge.cpp` et l'annotation de `wWinMain` ont disparu avec la réécriture |
| cppcheck | 0 | Les boucles brutes ont été remplacées par des algorithmes STL, la liste d'initialisation de `SimProHookService` est corrigée |

Deux alertes CodeQL concernaient la dépendance vendored **MinHook** (`third_party/minhook/src/buffer.c:229` : `cpp/type-confusion` et `cpp/suspicious-pointer-scaling`). Le dossier `third_party/` ayant été supprimé, ces alertes doivent disparaître du dépôt ; MinHook est désormais fourni par vcpkg et compilé hors de l'arbre des sources.

Les dépendances externes ne sont pas corrigées : elles sont suivies en amont. Le bruit éventuel est neutralisé par `/analyze:external-` et par le cache binaire vcpkg, qui évite de recompiler MinHook pendant l'analyse CodeQL.

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
- **cppcheck ne trouve rien** : vérifier que les dossiers de build sont bien exclus (`-i`), sinon les erreurs d'inclusion noient le rapport.
- **vcpkg introuvable** : définir `VCPKG_ROOT` (par exemple `C:\vcpkg`) ou installer le composant vcpkg de Visual Studio.

## 7. Dependabot

Les actions sont mises à jour chaque lundi, en une seule PR groupée (`ci(deps): …`). Dependabot ne lit la configuration que sur la branche par défaut : le fichier doit donc être présent sur `master`.
