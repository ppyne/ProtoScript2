# ProtoScript2 Lexical Specification (Consolidated)

Cette section définit officiellement la frontière lexicale et symbolique de ProtoScript2 pour le périmètre actuel du dépôt.

## 1. Boundary Definition

Référence: `docs/language_boundary.md`.

## 2. Cartographie enrichie

Référence: `docs/lexical_cartography_augmented.md`.

## 3. Règles normatives

- Chaque symbole est classé dans exactement un statut: `CORE`, `STDLIB`, `RUNTIME_ONLY`, `TEST_ONLY`.
- Tout symbole référencé dans la cartographie possède au moins une localisation vérifiable `fichier:ligne`.
- Les divergences frontend C/Node sont explicites et traçables.
- Le JSON machine-readable `docs/lexicon.json` est la source exploitable par outils.

## 4. Clarification des statuts

- `CORE`: langage et diagnostics de base.
- `STDLIB`: API modules standards exportées via registry non-test.
- `RUNTIME_ONLY`: symboles d’implémentation runtime non exposés comme surface standard.
- `TEST_ONLY`: symboles du harness/tests.

## Verification Notes

- Files analyzed: mêmes sources que `docs/language_boundary.md` et `docs/lexical_cartography_augmented.md`.
- Method: consolidation of generated boundary + augmented cartography + lexicon json.
- Ambiguities: runtime-only overlays remain implementation-dependent and are listed in divergence report.
- Limits: normative scope is bound to current repository state.
