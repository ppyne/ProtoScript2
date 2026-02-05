## Glossaire des acronymes et termes techniques

Cette section regroupe les acronymes et termes techniques utilisés dans la spécification, avec une définition opérationnelle et un exemple.

### ABI — Application Binary Interface

**Définition** : convention binaire définissant l’appel de fonctions, la disposition mémoire et la représentation des types au niveau machine.

**Exemple** : la spécification IR de ProtoScript V2 n’impose aucune ABI particulière afin de permettre différents backends.

---

### AST — Abstract Syntax Tree

**Définition** : représentation arborescente d’un programme après analyse syntaxique.

**Exemple** : le frontend Node.js construit un AST à partir du code source avant l’analyse statique.

---

### CFG — Control Flow Graph

**Définition** : graphe représentant les chemins d’exécution possibles d’une fonction.

**Exemple** : l’IR normatif impose un CFG valide avec des blocs terminés explicitement.

---

### CLI — Command Line Interface

**Définition** : interface en ligne de commande permettant d’interagir avec un outil logiciel.

**Exemple** : le compilateur ProtoScript V2 est fourni sous la forme d’un CLI en C.

---

### EBNF — Extended Backus–Naur Form

**Définition** : notation formelle permettant de décrire la grammaire d’un langage.

**Exemple** : l’annexe B définit la syntaxe complète de ProtoScript V2 en EBNF.

---

### GC — Garbage Collector

**Définition** : mécanisme automatique de gestion de la mémoire.

**Exemple** : ProtoScript V2 ne requiert pas de GC obligatoire ; la gestion mémoire est explicite ou déléguée au backend.

---

### IR — Intermediate Representation

**Définition** : représentation intermédiaire formelle entre le code source et le code généré.

**Exemple** : le frontend génère un IR normatif consommé par le backend C.

---

### OOB — Out Of Bounds

**Définition** : accès mémoire hors des limites valides d’une structure.

**Exemple** : un accès `list[i]` avec `i` hors bornes déclenche une exception runtime.

---

### RTTI — Run-Time Type Information

**Définition** : mécanisme permettant l’introspection dynamique des types à l’exécution.

**Exemple** : ProtoScript V2 interdit toute RTTI utilisateur (`instanceof`, downcast dynamique).

---

### SSA — Static Single Assignment

**Définition** : forme intermédiaire où chaque variable est assignée une seule fois.

**Exemple** : l’IR de ProtoScript V2 est inspiré des principes SSA sans en dépendre strictement.

---

### UB — Undefined Behavior

**Définition** : comportement non défini par la spécification, dépendant de l’implémentation.

**Exemple** : la partie normative élimine toute forme d’UB en définissant explicitement les cas limites.
