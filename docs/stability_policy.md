# ProtoScript2 — Politique cible pour ProtoScript2 1.0

Statut : PRE-STABILIZATION DRAFT

Applicable uniquement à partir de la version 1.0.0

## 1. Introduction

Cette politique définit officiellement la stabilité de l'API publique ProtoScript2, les règles de versioning, et les obligations de maintenance associées.

L'API publique normative ProtoScript2 est définie par les artefacts de référence suivants :

- `docs/language_boundary.md` (classification `CORE`, `STDLIB`, `RUNTIME_ONLY`, `TEST_ONLY`)
- `docs/lexicon.json` (inventaire machine-readable des symboles et statuts)
- `docs/protoscript2_spec_lexical.md` (consolidation normative lexicale)

ProtoScript2 adopte le versioning sémantique :

`MAJOR.MINOR.PATCH`

## 2. Versioning Scheme (SemVer obligatoire)

Règle normative :

- `MAJOR` : toute modification non rétro-compatible du `CORE` ou de la surface publique.
- `MINOR` : tout ajout rétro-compatible de fonctionnalité publique.
- `PATCH` : toute correction rétro-compatible sans extension de surface publique.

La version du langage (surface normative) peut être distincte de la version d'une implémentation (ex. runtime C, runtime Node). En cas d'écart, la compatibilité normative est évaluée contre les documents de référence cités en section 1.

## 3. Couches et impact sur la stabilité

### 3.1 CORE LANGUAGE

Le `CORE` inclut :

- mots-clés
- syntaxe
- types fondamentaux
- opérateurs
- méthodes natives des types fondamentaux
- diagnostics normatifs

Règles :

- suppression ou modification comportementale : `MAJOR++`
- ajout rétro-compatible : `MINOR++`
- correction sans modification sémantique observable : `PATCH++`

Justification : ces éléments sont classés `CORE` dans `docs/language_boundary.md` et `docs/lexicon.json`, donc contractuels pour la compatibilité.

### 3.2 STANDARD LIBRARY

La `STDLIB` inclut les modules non `test.*` du registry.

Règles :

- ajout rétro-compatible de fonction/module : `MINOR++`
- suppression ou changement de signature : `MAJOR++`

Justification : ces symboles sont classés `STDLIB` dans les artefacts normatifs et constituent une surface publique.

### 3.3 RUNTIME_ONLY

Les symboles `RUNTIME_ONLY` dépendent d'implémentation.

Règles :

- ils ne sont pas API stable par défaut
- leur évolution ne doit pas casser le `CORE`

Justification : classification explicite `RUNTIME_ONLY` dans `docs/language_boundary.md` et `docs/lexicon.json`.

### 3.4 TEST_ONLY

Les symboles `TEST_ONLY` ne sont pas couverts par les garanties de stabilité publique.

Justification : ces symboles sont dédiés au harness/tests et classés `TEST_ONLY` dans les artefacts normatifs.

## 4. Définition formelle d'une Breaking Change

Est une breaking change toute évolution qui viole la rétro-compatibilité contractuelle du `CORE` ou de la `STDLIB`, notamment :

- suppression d'un mot-clé `CORE`
- modification d'une règle de cast `CORE`
- changement du type de retour d'une méthode `CORE`
- suppression d'un code d'erreur normatif
- modification du comportement d'un opérateur `CORE`

Ces cas imposent `MAJOR++`.

## 5. Politique de dépréciation

Règles obligatoires :

- un élément public doit être marqué `deprecated` avant suppression
- la durée minimale de transition est d'au moins une version `MINOR`
- la dépréciation et la suppression doivent être documentées en release notes

La suppression effective après dépréciation reste une breaking change et impose `MAJOR++`.

## 6. Diagnostics et codes d'erreur

Les codes `E****` et `R****` sont définis comme partie de l'API stable.

Règles :

- suppression ou renommage d'un code normatif : `MAJOR++`
- ajout d'un nouveau code normatif : `MINOR++`
- correction interne sans changement du contrat du code existant : `PATCH++`

Justification : ces codes sont inventoriés dans `docs/lexicon.json` et classés `CORE`.

## 7. Processus de release

Étapes obligatoires :

1. Générer les artefacts lexicaux.
2. Exécuter les tests d'intégrité.
3. Vérifier `docs/divergence_report.md`.
4. Mettre à jour le changelog.
5. Valider le bump de version (`MAJOR|MINOR|PATCH`) selon cette politique.

La release est non conforme si une étape est omise.

## 8. Obligations des mainteneurs

Pour toute PR modifiant `CORE` ou `STDLIB`, les mainteneurs doivent :

- expliciter l'impact de versioning attendu
- justifier la classification (`CORE`, `STDLIB`, `RUNTIME_ONLY`, `TEST_ONLY`)
- mettre à jour `docs/lexicon.json` si la surface change
- exécuter et fournir le rapport des tests d'intégrité

Une PR est incomplète si ces éléments ne sont pas fournis.

## 9. Annexes

### 9.1 Checklist avant release

- classification de tous les nouveaux symboles validée
- artefacts (`language_boundary`, `lexicon`, `cartography`, `divergence`) régénérés
- test d'intégrité lexical exécuté sans écart
- changelog aligné avec la nature du bump
- bump de version validé contre cette politique

### 9.2 Scénarios de bump (exemples)

- ajout d'une fonction dans un module `STDLIB` sans rupture : `MINOR++`
- correction d'un bug sans changement de contrat public : `PATCH++`
- suppression d'une méthode `CORE` : `MAJOR++`
- modification incompatible d'un opérateur `CORE` : `MAJOR++`
- ajout d'un nouveau diagnostic normatif : `MINOR++`

### 9.3 Définition d'API publique ProtoScript2

L'API publique ProtoScript2 est l'ensemble des symboles classés `CORE` ou `STDLIB` dans `docs/lexicon.json`, interprétés selon `docs/language_boundary.md` et consolidés par `docs/protoscript2_spec_lexical.md`.

## Verification Notes

- Documents analysés : `docs/language_boundary.md`, `docs/lexicon.json`, `docs/protoscript2_spec_lexical.md`, `docs/divergence_report.md`.
- Décisions explicites prises :
  - adoption SemVer stricte (`MAJOR.MINOR.PATCH`)
  - qualification des codes `E****` / `R****` comme API stable
  - séparation normative entre version du langage et version d'implémentation
- Ambiguïtés restantes :
  - synchronisation exacte entre version de langage et version binaire d'implémentation lors de releases multi-runtime.
- Limites actuelles :
  - cette politique ne remplace pas le changelog; elle définit la règle de décision, pas le contenu historique des versions.
