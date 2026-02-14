# ProtoScript2 VS Code Extension

Extension VS Code officielle pour ProtoScript2, basée sur le frontend officiel du langage et un serveur LSP léger.

## Ce qui est embarqué

- Frontend ProtoScript2 officiel (même base d'analyse que le projet)
- Préprocesseur `mcpp` compilé en WASM et embarqué dans l'extension
- LSP autonome (diagnostics, complétion, aide de signature)

## Autonomie

Aucune dépendance système n'est requise pour l'usage de l'extension:

- pas de `mcpp` installé sur la machine
- pas de dépendance au repository source pour l'exécution
- pas d'outillage externe nécessaire à l'utilisateur final

## Artefact prêt à installer

Le package VSIX officiel est versionné dans ce repository:

- `protoscript2-0.1.0.vsix`

Vous pouvez installer directement ce fichier sans rebuild.

## Installation locale du VSIX

Prérequis:

- VS Code installé
- CLI `code` disponible dans le `PATH`

Depuis ce dossier:

```bash
cd tools/vscode/protoscript2
code --install-extension protoscript2-0.1.0.vsix
```

## Développement (optionnel)

Ces commandes sont utiles uniquement pour reconstruire l'extension:

```bash
cd tools/vscode/protoscript2
npm run build:all
npm test
```

## Extension de fichier

- `.pts`
