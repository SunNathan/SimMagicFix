# SimagicFix

![Icône SimagicFix](resources/icon-preview.png)

Correctif local pour la télémétrie **Le Mans Ultimate** sur **SimPro Manager 2.1.8** avec volant **Simagic FX Pro**.

SimagicFix lit les valeurs **TC**, **TC Cut**, **ABS**, **Motor Map**, **SoC** et **ARB avant/arrière** dans la mémoire partagée de LMU, puis les écrit directement dans ce buffer.

## Compatibilité

| Élément | Version prise en charge |
| --- | --- |
| Windows | 10 / 11 x64 |
| SimPro Manager | **2.1.8** (bases Alpha) |
| Le Mans Ultimate | 1.3 ou plus récent |
| Droits administrateur | non requis |

> Le correctif dépend de l'agencement interne de SimPro 2.1.8. Une mise à jour de SimPro peut le rendre inactif jusqu'à adaptation.

## Installation

1. Télécharger l'archive de release et la décompresser dans un dossier permanent.
2. Lancer `SimagicFix.exe`.
3. Cocher éventuellement « Lancer SimagicFix au démarrage de Windows ».
4. Cliquer sur **Installer SimagicFix**.
5. Une fenêtre d'avertissement explique que les fichiers `SimagicFix.exe` et `SimagicFixHook.dll` du dossier de lancement seront supprimés après l'installation. Valider pour continuer.
6. L'icône de SimagicFix apparaît dans la zone de notification.

L'installation copie l'application dans `%LOCALAPPDATA%\SimagicFix`, crée un raccourci dans le menu Démarrer et, si demandé, active le démarrage automatique. Les fichiers source du dossier de lancement sont ensuite supprimés : la DLL immédiatement, l'exe à la fermeture de l'application. L'application installée et le raccourci restent fonctionnels.

## Utilisation

1. Lancer **SimPro Manager**, puis **Le Mans Ultimate** et entrer en piste.
2. Ouvrir SimagicFix (double-clic sur l'icône de la zone de notification).
3. Cocher **« Activer le patch memoire SimPro (hook) »**.
4. La ligne **Hook SimPro** indique :
   - `actif` : le volant reçoit les valeurs LMU ;
   - `en attente` : SimPro n'est pas encore lancé ou la session n'est pas ouverte ;
   - `desactive` : le patch est décoché.

Les valeurs du volant suivent alors le MFD de la voiture : TC, TC Cut, ABS, Motor Map, SoC et ARB avant/arrière.

### Menu de la zone de notification

- **Ouvrir SimagicFix** : affiche la fenêtre.
- **Démarrage automatique** : active ou désactive le lancement avec Windows.
- **Ouvrir les logs** : ouvre `%LOCALAPPDATA%\SimagicFix\logs`.
- **Désinstaller** : arrête le hook, retire l'auto-démarrage, le raccourci et les fichiers installés.
- **Quitter** : ferme l'application.

L'état (installation, patch, auto-démarrage) est mémorisé dans `state.json` et restauré au lancement suivant.

## Sécurité

- Lecture **en lecture seule** de la mémoire partagée officielle de LMU.
- Aucune écriture dans le processus du jeu et aucun fichier LMU modifié.
- Le patch écrit uniquement dans le processus **SimPro**, via une DLL injectée dans SimPro uniquement.
- Aucun hook dans le processus du jeu.
- Aucun fichier SimPro n'est modifié.

## Dépannage

### Le statut reste « en attente »

- Vérifier que SimPro Manager 2.1.8 est lancé.
- Vérifier que la version de SimPro est bien **2.1.8** (une version plus récente n'est pas prise en charge).
- Consulter les logs : **Ouvrir les logs**, puis `app.log`.

### Le volant affiche encore des valeurs figées

- Vérifier que la case **« Activer le patch memoire SimPro (hook) »** est cochée.
- Vérifier que LMU est en session (pas dans les menus) et que la voiture est active.
- Certaines voitures n'ont pas d'ABS ou d'hybride : les valeurs correspondantes peuvent rester à 0, c'est normal.
- Redémarrer SimPro puis SimagicFix.

### Windows SmartScreen ou l'antivirus bloque l'application

L'exécutable n'est pas signé et l'injection de DLL peut être signalée. Autoriser explicitement `SimagicFix.exe` et `SimagicFixHook.dll`, ou compiler le projet soi-même.

### Le hook ne s'installe plus après une mise à jour

Les adresses et offsets sont spécifiques à SimPro 2.1.8. Une mise à jour de SimPro nécessite une nouvelle analyse et une nouvelle version de SimagicFix.

## Désinstallation

- Depuis la fenêtre ou le menu de la zone de notification : **Désinstaller**.
- Ou depuis Windows : Paramètres → Applications → SimagicFix.

Si SimPro Manager est encore lancé, SimagicFix demande de le fermer avant de continuer. Les options sont **fermer SimPro et continuer** ou **annuler**. Si SimPro ne se ferme pas, la désinstallation est annulée avec un message clair.

La désinstallation arrête le hook, retire le démarrage automatique et le raccourci du menu Démarrer.

## Licence

MIT. Voir `LICENSE`.

### Licences tierces

L'archive de livraison contient `SimagicFix.exe`, `SimagicFixHook.dll`, `LICENSE` et ce `README.md`. Les notices ci-dessous sont donc incluses dans ce README pour que la distribution binaire reste conforme.

#### nlohmann/json

Copyright (c) 2013-2022 Niels Lohmann

MIT License. Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

#### MinHook

MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov
(Hacker Disassembler Engine 32/64 C). The HDE 32/64 C license is the same
BSD 2-Clause license and disclaimer reproduced above.

Simagic est une marque de Simagic ; ce projet est non officiel et n'est pas affilié à Simagic.

---

Documentation développeur : [`README.dev.md`](README.dev.md).
