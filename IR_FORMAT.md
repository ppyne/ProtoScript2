![ProtoScript2](header.png)

# ProtoScript V2 — IR Serialized Format

Version courante : `1.0.0`

Ce document définit le contrat de sérialisation JSON de l’IR normatif.

## 1. Enveloppe

Le document JSON doit avoir la forme :

```json
{
  "ir_version": "1.0.0",
  "format": "ProtoScriptIR",
  "module": { "...": "..." }
}
```

Contraintes :

- `format` MUST être `ProtoScriptIR`
- `ir_version` MUST être `1.0.0` (pour cette version)
- `module.kind` MUST être `Module`

## 2. Structure minimale

`module` contient :

- `functions`: array de `Function`

`Function` contient :

- `kind`: `Function`
- `name`: string non vide
- `params`: array
- `returnType`: `IRType`
- `blocks`: array non vide de `Block`

`IRType` contient :

- `kind`: `IRType`
- `name`: string (ex: `int`, `list<int>`, `view<int>`)
- `repr`: optionnel (ex: `(ptr,len)`, `(ptr,len,cap)`)

`Block` contient :

- `kind`: `Block`
- `label`: string non vide, unique dans la fonction
- `instrs`: array d’instructions

## 3. Invariants validés

Le validateur structurel vérifie :

- labels de blocs uniques par fonction
- cibles de `jump`/`branch_if`/`branch_iter_has_next` existantes
- `op` connue pour chaque instruction

## 4. Outils CLI

Émettre l’IR JSON :

```bash
bin/protoscriptc --emit-ir-json file.pts
```

Valider un document IR JSON :

```bash
bin/protoscriptc --validate-ir file.json
```

## 5. Politique d’évolution

- Toute rupture de compatibilité du schéma MUST changer `ir_version`.
- Les changements backward-compatible SHOULD conserver la lecture des anciens champs.
- Les backends C MUST valider le document IR avant consommation.
