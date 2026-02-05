![ProtoScript2](header.png)

# ProtoScript V2 — Implementation Strategy

Ce document fixe la stratégie d’implémentation pour éviter les régressions de sémantique pendant la transition vers une CLI native en C.

## 1. Rôles des implémentations

### Node.js (référence)

Node.js est l’implémentation canonique de la spécification :

- parseur conforme EBNF
- analyse statique normative
- diagnostics normatifs
- IR normatif
- validation via conformance kit

Statut : **oracle sémantique** (spec vivante), pas cible produit final.

### C (production)

La CLI C est l’implémentation de production :

- backend concret
- exécution système
- crédibilité bas niveau
- performance et intégration native

Statut : **preuve d’implémentabilité bas niveau**.

## 2. Principe directeur

Node.js n’est pas une implémentation concurrente du C.  
Node.js est la spec vivante.  
Le C est la preuve que la spec est vraie.

## 3. Pourquoi maintenir Node.js en parallèle

Ce que Node apporte déjà est critique :

- vérité sémantique testée
- diagnostics stabilisés
- arbitrages language-level validés

Le supprimer pendant le portage C rendrait impossible l’attribution claire des bugs :

- bug de spec ?
- bug de backend C ?
- bug de frontend C ?

Sans oracle, cette distinction devient coûteuse et fragile.

## 4. Pourquoi construire la CLI C maintenant

Rester uniquement sur Node serait incohérent avec le positionnement de ProtoScript V2 :

- compilation C prioritaire
- coûts mémoire explicités
- modèle runtime sobre

La CLI C est donc nécessaire, mais **pas** au prix de perdre l’oracle Node.

## 5. Architecture recommandée (phase actuelle)

Pipeline officiel :

```text
ProtoScript source
        |
        v
 Node.js frontend
 (parse + analyse + IR)
        |
        v
   IR normatif sérialisé
        |
        v
      CLI C
   (backend + runtime)
```

Conséquence immédiate :

- le frontend C complet (lexer/parser) n’est pas prioritaire au début
- la priorité est la convergence sémantique backend/runtime

## 6. Phases de migration

### Phase A — Séparation stricte

- Node produit l’IR normatif
- C consomme l’IR et produit C/exécute
- aucune logique sémantique dupliquée sans nécessité

### Phase B — Validation croisée

Pour chaque test normatif :

1. Node : parse + analyse + IR  
2. C : consomme IR + backend/runtime  
3. comparer :
   - accept/reject
   - diagnostics (catégorie/code/position)
   - comportement observable

Règle : tant que divergence, considérer d’abord un bug côté C.

### Phase C — Autonomie C (plus tard)

Seulement après :

- backend C stable
- 100% de conformance
- opt-safety verte

Alors :

- implémenter lexer C
- implémenter parser C
- remplacer progressivement le frontend Node dans le chemin produit

Node reste maintenu comme validateur/oracle.

## 7. Règles non négociables

- Ne jamais activer les optimisations avant conformité complète et backend stable.
- Ne jamais accepter une divergence observable entre Node et C.
- Ne jamais supprimer les checks runtime normatifs sans preuve statique.
- Ne pas “simplifier” la spec pour contourner un bug backend.

## 8. Décisions opérationnelles immédiates

1. Geler Node comme frontend de référence.
2. Stabiliser le format IR sérialisé (JSON lisible d’abord, cf. `IR_FORMAT.md`).
3. Faire évoluer la CLI C en priorité sur backend/runtime.
4. Exécuter conformance + opt-safety à chaque jalon significatif.
