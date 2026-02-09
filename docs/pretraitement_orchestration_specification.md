# ProtoScript2 — Spécification normative
## Prétraitement et orchestration des outils

---

## 1. Portée du document

Ce document spécifie le rôle et les règles du **prétraitement** dans l’écosystème ProtoScript2.

Il ne décrit **ni un nouveau langage**, ni une extension syntaxique de ProtoScript2.
Le prétraitement est défini comme une **phase externe**, orchestrée par les outils (CLI, compilateur), et **hors du langage**.

---

## 2. Principe fondamental

ProtoScript2 repose sur une séparation stricte des responsabilités :

- le **langage** définit une syntaxe et une sémantique stables,
- les **outils** orchestrent les phases nécessaires à l’exécution ou à la compilation,
- le **prétraitement**, lorsqu’il est utilisé, est une phase préalable et externe.

> **Le langage ProtoScript2 ne connaît pas le préprocesseur.**

---

## 3. Définition du prétraitement

Le prétraitement est une transformation textuelle appliquée à des sources avant toute analyse syntaxique ProtoScript2.

Caractéristiques normatives :

- transformation **purement textuelle**,
- absence totale de sémantique ProtoScript2,
- aucune interaction avec le système de types,
- production exclusive de code ProtoScript2 valide.

---

## 4. Artefact d’entrée et de sortie

### 4.1 Entrée

Les outils peuvent accepter en entrée :

- des fichiers ProtoScript2 (`.pts`),
- ou des fichiers contenant des directives de prétraitement (ex. templates).

La nature exacte de ces directives n’est **pas** définie par la spécification du langage.

### 4.2 Sortie

Le résultat du prétraitement doit être :

- un fichier ProtoScript2 valide,
- sans directives de prétraitement résiduelles,
- directement analysable par le parseur ProtoScript2.

---

## 5. Orchestration par les outils

### 5.1 Rôle du CLI

Le CLI ProtoScript2 agit comme un **driver**.

Lorsqu’il est invoqué pour interpréter un programme :

1. il détermine si une phase de prétraitement est requise,
2. il exécute un préprocesseur externe approprié,
3. il utilise exclusivement le code ProtoScript2 résultant.

Le CLI **n’interprète jamais** de directives de prétraitement.

---

### 5.2 Rôle du compilateur

Le compilateur ProtoScript2 suit les mêmes règles que le CLI.

Toute phase de prétraitement éventuelle est exécutée **avant** la compilation proprement dite.

Le compilateur ne dépend pas du préprocesseur et ne connaît que du ProtoScript2 valide.

---

## 6. Unicité sémantique

Quel que soit le chemin d’exécution (interprétation ou compilation) :

- le même code ProtoScript2 prétraité doit être obtenu,
- la sémantique observable doit être identique,
- aucune divergence de comportement n’est autorisée.

> **Le prétraitement ne doit jamais influencer la sémantique du langage.**

---

## 7. Modules et prétraitement

Un module ProtoScript2 importable est défini comme :

- un fichier ProtoScript2 valide,
- ne contenant aucune directive de prétraitement,
- indépendamment de son mode de production (écrit à la main ou généré).

Le système de modules opère exclusivement sur des artefacts déjà prétraités.

---

## 8. Mise en cache et reproductibilité

Les outils peuvent :

- mettre en cache les résultats du prétraitement,
- éviter les transformations répétées,
- garantir la reproductibilité des builds.

Les mécanismes de cache sont laissés à l’implémentation.

---

## 9. Compatibilité des préprocesseurs

Il est permis d’utiliser :

- un préprocesseur pour la compilation,
- un autre pour l’interprétation,

à condition que :

- leur contrat soit équivalent,
- leur sortie soit sémantiquement identique.

---

## 10. Non-objectifs

Cette spécification n’impose pas :

- un préprocesseur spécifique,
- une syntaxe de directives particulière,
- un langage de templates dédié.

Ces choix relèvent de l’outillage, non du langage.

---

## 11. Résumé normatif

- Le prétraitement est externe au langage.
- Les outils peuvent l’orchestrer automatiquement.
- Le langage ProtoScript2 ne voit que du code valide.
- L’interpréteur et le compilateur partagent la même réalité sémantique.

---

## 12. Conclusion

Cette architecture permet :

- un langage simple et mécanique,
- une ergonomie moderne,
- une extensibilité par les outils,
- et une adoption facilitée.

Le prétraitement est assumé comme une **responsabilité de l’écosystème**, non du langage.

