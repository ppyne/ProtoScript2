![ProtoScript2](header.png)

# ProtoScript V2 — Rationale

Ce document explique les décisions de conception qui peuvent paraître restrictives à première vue.

Objectif : conserver une compréhension stable du langage pour :

- les reviewers
- les contributeurs
- le mainteneur futur (y compris soi-même dans 2 ans)

Ce texte ne remplace pas la spécification normative (`SPECIFICATION.md`) : il en donne les motivations techniques.

---

## 1) Pourquoi pas de fonctions comme valeurs

Décision :

- les fonctions ne sont ni stockables, ni passables, ni retournables

Ce que cela évite :

- closures et capture implicite de contexte
- coût caché d’allocation et de durée de vie
- polymorphisme d’appel difficile à raisonner

Ce que cela apporte :

- résolution d’appel entièrement statique
- signatures explicites, analyzables localement
- traduction directe et lisible vers C

Compromis assumé :

- moins d’expressivité “fonctionnelle”
- plus de verbosité via prototypes/méthodes dédiées

Pourquoi ce compromis est acceptable ici :

- ProtoScript V2 privilégie la prévisibilité et la compilabilité transparente, pas la concision maximale.

---

## 2) Pourquoi pas de fallthrough dans `switch`

Décision :

- chaque `case` / `default` doit se terminer explicitement (`break`, `return`, `throw`, etc.)
- le fallthrough implicite est interdit

Ce que cela évite :

- bugs historiques liés à un `break` oublié
- dépendances de contrôle implicites entre branches
- relecture coûteuse de grands `switch`

Ce que cela apporte :

- flux de contrôle local et explicite
- invariants statiques simples (chaque clause est autonome)
- diagnostics plus clairs

Compromis assumé :

- pas d’astuce “chaîner des cases” sans duplication

Pourquoi ce compromis est acceptable ici :

- le langage favorise la lisibilité stricte sur les raccourcis de style C legacy.

---

## 3) Pourquoi pas d’affectation chaînée

Décision :

- l’affectation est une instruction sans valeur
- `a = b = c` est invalide

Ce que cela évite :

- effets de bord implicites dans des expressions complexes
- ambiguïtés de lecture sur ordre d’évaluation et intention
- complexité inutile dans l’analyse statique

Ce que cela apporte :

- séparation nette : calcul de valeur vs mutation d’état
- intentions explicites ligne par ligne
- modèle plus simple à vérifier et à optimiser sans surprise

Compromis assumé :

- style parfois plus verbeux

Pourquoi ce compromis est acceptable ici :

- ProtoScript V2 vise la robustesse des bases de code longues, pas le code-golf.

---

## 4) Pourquoi pas de RTTI utilisateur

Décision :

- pas de `instanceof`, pas de downcast dynamique, pas d’introspection de type utilisateur
- exception : métadonnée interne de type réservée au mécanisme `catch`

Ce que cela évite :

- logique métier dépendante du type réel runtime
- “escapes” qui cassent les garanties statiques
- surcoûts structurels de systèmes RTTI généralisés

Ce que cela apporte :

- le type statique gouverne les accès partout
- comportements déterministes et prévisibles
- optimisation backend plus sûre (pas de dépendance RTTI cachée)

Compromis assumé :

- moins de flexibilité pour certains patterns orientés introspection

Pourquoi ce compromis est acceptable ici :

- la philosophie du langage est : exprimer la variabilité par la structure statique, pas par l’inspection dynamique.

---

## 5) Structures de données : lecture stricte, écriture constructive

Décision :

- les accès de lecture vérifient l’existence de la donnée ciblée
- les écritures construisent l’état attendu du conteneur selon sa règle normative

Application principale (`map<K,V>`) :

- `map[k]` en lecture : la clé doit exister, sinon exception runtime explicite
- `map[k] = v` en écriture : insertion si la clé est absente, mise à jour si elle est présente

Pourquoi ce choix :

- éviter les valeurs implicites ambiguës en lecture
- garantir une mutation utile et déterministe en écriture
- aligner la sémantique avec la règle générale du langage : explicite, prévisible, vérifiable

---

## Synthèse

Ces décisions partagent un même principe :

- supprimer les ambiguïtés qui déplacent la complexité du code vers le runtime ou vers le lecteur.
- appliquer une règle simple sur les structures : **lecture stricte, écriture constructive**.

Lecture stricte, écriture constructive (cas `map`) :

- `map[k]` en lecture exige que `k` existe (sinon exception explicite)
- `map[k] = v` en écriture construit l’état attendu (insertion si absente, mise à jour si présente)

ProtoScript V2 préfère :

- des règles plus strictes,
- des intentions plus explicites,
- une compilation plus directe.

Le coût est une expressivité “magique” plus faible.
Le bénéfice est une base technique plus stable, plus lisible et plus vérifiable.
