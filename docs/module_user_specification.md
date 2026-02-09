# Modules ProtoScript (utilisateur) — Spécification normative (ProtoScript V2)

Ce document définit un format **normatif** pour permettre l’import de modules **développés en ProtoScript V2** par les utilisateurs.
Il ne modifie pas la syntaxe `import` existante et respecte la résolution **statique**.

## 1. Objectif

Définir de façon simple, claire et déterministe comment :

- déclarer un module,
- le localiser sur disque,
- l’importer dans un script ProtoScript2,
- l’utiliser dans la compilation statique du langage.

Ce mécanisme **ne concerne que les modules écrits en ProtoScript2**.

Il est **totalement distinct** du système de modules natifs développés en C :

- les modules C répondent à des contraintes d’ABI, de performance et d’interopérabilité bas niveau,
- les modules ProtoScript2 répondent à des besoins quotidiens de structuration, de lisibilité et de réutilisation du code.

Les deux mécanismes coexistent volontairement, chacun étant adapté à son domaine d’usage.

---

## 2. Concepts

### 2.1 Module

Un **module ProtoScript2** est un fichier `.pts` contenant un **prototype racine public** et rien d'autres (pas de fonction à la racine).

- Aucun manifeste séparé.
- Aucune déclaration d’exports.
- Le prototype racine constitue l’API du module.
- Le nom logique du module est dérivé du nom du prototype racine.

---

### 2.2 Prototype racine

- Contient les fonctions, types et constantes publiques du module.
- Est le point d’entrée unique du module.
- Doit être unique par fichier module.

---

## 3. Structure d’un module

Un fichier `.pts` représentant un module doit contenir exactement un prototype racine.

La notion de **ModuleDirectory** est **logique**, non obligatoire, et sert uniquement à l’organisation des sources sur disque (stockage).

- Un module peut être situé :

  - dans un sous-répertoire dédié (`<ModuleDirectory>/<ModuleName>.pts`),
  - directement à proximité du script qui l’importe,
  - dans l’un des chemins définis par `search_paths` (voir concept ci-dessous).

- `ModuleDirectory` peut être :

  - un chemin relatif au script racine compilé,
  - un chemin absolu,
  - ou un chemin relatif à l’un des `search_paths`.

Structure possible :

```text
<ModuleDirectory>/<ModuleName>.pts
```

ou simplement :

```text
<ModuleName>.pts
```

Exemple :

```c
// Stack.pts
prototype Stack {

    // variable publique du module
    var defaultCapacity : int = 16;

    function create() : Stack {
        Stack s;
        s.items = [];
        return s;
    }

    function push(Stack s, int value) : void {
        s.items.push(value);
    }

    function pop(Stack s) : int {
        int v = s.items[s.items.length - 1];
        s.items.removeLast();
        return v;
    }

    function isEmpty(Stack s) : bool {
        return s.items.length == 0;
    }
}
```

---

## 4. Résolution des modules

### 4.1 Registre des chemins

Un fichier `registry.json` peut définir une liste ordonnée des chemins où le compilateur recherche les modules ProtoScript2 **importés par nom logique** :

```json
{
  "search_paths": [
    "./modules",
    "./vendor",
    "/usr/local/lib/protoscript",
    "/opt/ps-collections"
  ]
}
```

Clarification :

- l’usage du `registry.json` pour les **modules natifs C** reste **inchangé** ;
- `search_paths` est une **extension non conflictuelle** utilisée uniquement pour les modules ProtoScript2.

Règles :

- Les chemins relatifs sont résolus par rapport au script racine compilé.
- Les chemins absolus sont utilisés tels quels.
- L’ordre des chemins détermine la priorité de résolution.
- Aucun scan récursif implicite n’est effectué hors de ces chemins.

---

## 5. Importation

Le mécanisme d’import des modules ProtoScript2 **reprend exactement** la syntaxe et les usages des imports de modules standards (modules natifs en C) décrits dans le manuel de référence.

Il n’existe **aucune syntaxe spécifique** pour les modules ProtoScript2 : seul le mécanisme de résolution (fichier `.pts` + prototype racine) diffère.

Les formes d’import suivantes sont donc toutes valides et identiques dans leur sémantique :

```c
import Io;
import Io as io;
import Math.{abs, sqrt as racine};
import JSON.{encode, decode};
```

La différence entre un module natif et un module ProtoScript2 est **transparente pour l’utilisateur** au moment de l’import.

---

### Principe général

Il n’existe **pas de conflit ambigu** lors de la résolution des modules ProtoScript2.

- Un module utilisateur est **toujours** un fichier se terminant par `.pts`.
- La recherche des modules suit **exclusivement** les chemins définis dans `search_paths`.
- Les chemins sont parcourus **dans l’ordre**.
- **Le premier module trouvé est celui qui est utilisé.**

Il n’y a donc ni surcharge, ni résolution multiple, ni fusion : la résolution est strictement déterministe.

---

### 5.1 Import par nom logique

Syntaxe :

```c
import datastruct.Stack;
```

Résolution :

1. Le compilateur parcourt `search_paths` dans l’ordre.
2. Pour chaque chemin, il recherche un fichier correspondant à :
   ```
   <path>/datastruct/Stack.pts
   <path>/Stack.pts
   ```
3. Le premier fichier trouvé est sélectionné.
4. Le compilateur vérifie la présence du prototype racine `Stack`.

Si aucun fichier n’est trouvé → erreur statique.

---

### 5.2 Import par chemin explicite

Un module peut être importé directement par son chemin :

```c
import "./lib/math/Math.pts";
import "/opt/ps-collections/math/Math.pts";
import "./datastruct/Stack.pts".{create, push};
```

Règles :

- Le chemin doit référencer un fichier `.pts`.
- Chemin relatif : résolu par rapport au fichier courant.
- Chemin absolu : utilisé tel quel.
- Le prototype racine du fichier reste obligatoire et est vérifié statiquement.
- Aucune recherche `search_paths` n’est effectuée pour un import par chemin explicite.
- Pour un import sélectif (`.{...}`), les symboles référencés doivent exister dans le prototype racine.

---

## 6. Résolution statique

- Tous les imports sont résolus à la compilation.
- Aucun chargement dynamique n’est autorisé à l’exécution.
- Les dépendances cycliques entre modules sont interdites.
- Les modules sont intégrés statiquement dans l’arbre de compilation.

Erreurs statiques spécifiques :

- `E2002` `IMPORT_PATH_NOT_FOUND` : chemin introuvable ;
- `E2003` `IMPORT_PATH_BAD_EXTENSION` : fichier sans extension `.pts` ;
- `E2004` `IMPORT_PATH_NO_ROOT_PROTO` : fichier ne contenant pas exactement un prototype racine public.

---

## 7. Avantages du modèle

### Ergonomie

- Un module se lit et se comprend uniquement via son fichier `.pts`.
- Aucune duplication de signatures.
- Aucune métadonnée parasite.

### Cohérence conceptuelle

- Alignement strict avec le modèle prototype-based de ProtoScript2.
- Le prototype racine définit naturellement la surface publique du module.

### Interopérabilité

- Modules utilisables depuis des emplacements multiples.
- Support des collections figées (vendor, stdlib, dépôts externes).
- Builds reproductibles et déterministes.

---

## 8. Règles de compilation

1. Charger `registry.json`.
2. Résoudre chaque import (nom logique ou chemin explicite).
3. Vérifier l’unicité et la cohérence du prototype racine.
4. Intégrer statiquement les modules.
5. Générer le code cible (ex. `emit-c`).

---

## 9. Terminologie

| Terme            | Définition                                  |
| ---------------- | ------------------------------------------- |
| Module           | Fichier `.pts` avec prototype racine public |
| Prototype racine | API publique du module                      |
| Registry         | Fichier listant les chemins de recherche    |
| Import           | Référence statique à un module              |

---

## 10. Exemple complet

L’exemple suivant utilise une **structure de données** afin d’éviter toute confusion avec les modules natifs existants (par exemple `Math`).

### `registry.json`

```json
{
  "search_paths": [
    "./modules",
    "./vendor/protoscript-stdlib",
    "/opt/protoscript/collections"
  ]
}
```

### Structure des fichiers

```
modules/
  datastruct/
    Stack.pts
```

### Module `Stack`

```c
// modules/datastruct/Stack.pts
prototype Stack {

    function create() : Stack {
        Stack s;
        s.items = [];
        return s;
    }

    function push(Stack s, int value) : void {
        s.items.push(value);
    }

    function pop(Stack s) : int {
        int v = s.items[s.items.length - 1];
        s.items.removeLast();
        return v;
    }

    function isEmpty(Stack s) : bool {
        return s.items.length == 0;
    }
}
```

### Utilisation

```c
import datastruct.Stack;

function main() : void {
    Stack s = Stack.create();

    Stack.push(s, 10);
    Stack.push(s, 20);

    int x = Stack.pop(s);   // 20
    int y = Stack.pop(s);   // 10
}
```

Cet exemple illustre qu’un module ProtoScript2 :

- est défini par un prototype racine,
- expose naturellement ses opérations,
- ne nécessite aucune déclaration d’export,
- reste entièrement résolu à la compilation.

---

## Fin de la spécification
