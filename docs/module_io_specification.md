# ProtoScript2 — Module Io Specification

Status: Normative  
Scope: POSIX systems only (Linux, BSD, macOS)

---

# 1. Overview

Le module `Io` fournit les primitives d’entrées/sorties synchrones de ProtoScript2.

Objectifs principaux :

- I/O strictement synchrone
- sémantique explicite et déterministe
- séparation stricte texte / binaire
- durée de vie des ressources manuelle (close explicite)
- UTF-8 strict en mode texte
- aucune conversion implicite texte ↔ binaire
- aucun buffering implicite
- aucun flush implicite

Le module `Io` est un module natif chargé via le système de modules ProtoScript2.

La disponibilité est déterminée par le registre des modules (résolution statique des imports) et par la présence du binaire natif correspondant au runtime.

---

# 2. Error Model and ABI Mapping

## 2.1 Exceptions (sémantique langage)

ProtoScript2 représente les erreurs runtime comme des objets d’exception.

- Seules des instances de prototypes dérivant de `Exception` peuvent être levées.
- Toute erreur issue du runtime ou d’un module natif doit lever une instance dérivant de `RuntimeException`.
- `catch (T e)` matche par type de prototype (substitution parent/enfant).

## 2.2 Prototypes d’exception Io (contrat module)

`Io` définit les prototypes d’exception suivants (prototypes scellés, fournis par le module) :

- `InvalidModeException : RuntimeException`
- `FileOpenException : RuntimeException`
- `FileNotFoundException : RuntimeException`
- `PermissionDeniedException : RuntimeException`
- `InvalidPathException : RuntimeException`
- `FileClosedException : RuntimeException`
- `InvalidArgumentException : RuntimeException`
- `InvalidGlyphPositionException : RuntimeException`   
- `ReadFailureException : RuntimeException`
- `WriteFailureException : RuntimeException`
- `Utf8DecodeException : RuntimeException`
- `StandardStreamCloseException : RuntimeException`

Règle normative : toute erreur mentionnée dans cette spécification DOIT lever une des exceptions ci-dessus.

## 2.3 Exigences ABI natives

Le code natif DOIT signaler une erreur via l’ABI publique :

- appeler `ps_throw(ctx, <PS_ERR_*>, <message>)`
- retourner `PS_ERR`

Le runtime construit ensuite l’objet d’exception et sélectionne le prototype d’exception Io approprié.

---

# 3. Principes texte / binaire

- Un fichier texte opère sur des `string`.
- Un fichier binaire opère sur des `list<byte>`.
- Aucune conversion implicite n’existe entre ces deux mondes.

Mode texte :

- lecture : décodage UTF-8 strict
- positions/taille : exprimées en **glyphes**

Mode binaire :

- lecture : octets bruts
- positions/taille : exprimées en **octets**

---

# 4. Interface du module

```ps
import Io;
```

Le module expose :

- des fonctions globales
- deux prototypes fermés : `TextFile` et `BinaryFile`
- une constante `Io.EOL`
- trois flux standards : `Io.stdin`, `Io.stdout`, `Io.stderr`

Les prototypes `TextFile` et `BinaryFile` sont fermés : aucun champ public, uniquement les méthodes spécifiées ici.

---

# 5. Modes d’ouverture

Le paramètre `mode` est une `string`.

Modes normatifs (exacts) :

- `"r"` lecture
- `"w"` écriture (truncate)
- `"a"` écriture (append)

Toute autre valeur DOIT lever `InvalidModeException`.

---

# 6. Io.openText

## Io.openText(path: string, mode: string) : TextFile

Ouvre un fichier texte et retourne un handle `TextFile`.

Sémantique texte :

- lecture/écriture de `string`
- décodage UTF-8 strict en lecture
- aucun traitement implicite de fin de ligne
- aucun traitement implicite de BOM (un BOM est un glyphe)

Throws :

- `InvalidModeException`
- `InvalidPathException`
- `FileNotFoundException` (si requis par le mode)
- `PermissionDeniedException`
- `FileOpenException`

---

# 7. Io.openBinary

## Io.openBinary(path: string, mode: string) : BinaryFile

Ouvre un fichier binaire et retourne un handle `BinaryFile`.

Sémantique binaire :

- lecture/écriture de `list<byte>`
- aucun décodage

Throws :

- `InvalidModeException`
- `InvalidPathException`
- `FileNotFoundException` (si requis par le mode)
- `PermissionDeniedException`
- `FileOpenException`

---

# 8. Io.tempPath

## Io.tempPath() : string

Retourne un chemin temporaire unique et inexistant.

Comportement normatif :

- retourne une `string`
- le chemin DOIT être valide pour le système courant
- le chemin DOIT être inexistant au moment du retour
- la fonction NE DOIT PAS créer le fichier
- deux appels successifs DOIVENT retourner deux chemins distincts
- répertoire temporaire utilisé :
  - POSIX : `$TMPDIR` sinon `/tmp`

Propriétés assumées :

- aucune réservation durable n’est effectuée
- aucune suppression implicite n’est effectuée
- la fonction ne protège pas contre une race condition externe

Throws :

- `IOException` (échec système)

---

# 9. Flux standards

Les flux standards suivants sont fournis comme `TextFile` déjà ouverts :

- `Io.stdin` (lecture)
- `Io.stdout` (écriture)
- `Io.stderr` (écriture)

Règles normatives :

- ces flux sont en mode texte
- ces flux NE DOIVENT PAS être fermés par le code utilisateur
- `close()` sur l’un d’eux DOIT lever `StandardStreamCloseException`
- durée de vie : celle du processus

---

# 10. Constantes

## Io.EOL : string

```ps
Io.EOL == "\n"
```

---

# 11. Io.print et Io.printLine

## 11.1 Io.print(value) : void

Écrit sur `Io.stdout` sans fin de ligne.

Conversion normative :

1. Si `value` est de type `string`, écrire la valeur telle quelle.
2. Sinon, appeler `value.toString()`.
3. `toString()` DOIT retourner une `string`.
4. Si `toString()` n’existe pas, ou ne retourne pas une `string`, lever `InvalidArgumentException`.

Aucune coercition globale implicite (pas de `String(value)` magique).

Throws :

- `InvalidArgumentException`
- `WriteFailureException` (cf. §14)

## 11.2 Io.printLine(value) : void

Écrit `value` puis `Io.EOL`.

Conversion : identique à `Io.print`.

Throws :

- `InvalidArgumentException`
- `WriteFailureException`

---

# 12. TextFile API

## 12.1 TextFile.read(size: int) : string

Lit `size` glyphes à partir de la position courante.

Règles :

- `size` DOIT être un int strictement positif (`>= 1`)
- EOF n’est pas une erreur

Retour :

- `string`
- `length == 0` indique EOF

Throws :

- `InvalidArgumentException`
- `FileClosedException`
- `Utf8DecodeException`
- `ReadFailureException`

## 12.2 TextFile.write(text: string) : void

Écrit `text` à la position courante.

Règles :

- `text` DOIT être une `string`
- aucune fin de ligne implicite

Atomicité et erreurs : cf. §14.

Throws :

- `InvalidArgumentException`
- `FileClosedException`
- `WriteFailureException`

## 12.3 TextFile.tell() : int

Retourne la position courante en glyphes.

Throws :

- `FileClosedException`
- `ReadFailureException` (si l’OS ne permet pas l’opération)

## 12.4 TextFile.seek(pos: int) : void

Positionne le curseur à `pos` glyphes.

Règles :

- `pos` DOIT être un int `>= 0`
- si `pos > size()`, lever `InvalidGlyphPositionException`

Throws :

- `InvalidArgumentException`
- `InvalidGlyphPositionException`
- `FileClosedException`
- `ReadFailureException`

## 12.5 TextFile.size() : int

Retourne la taille du fichier en glyphes.

Throws :

- `FileClosedException`
- `ReadFailureException`

## 12.6 TextFile.name() : string

Retourne le chemin/nom associé au handle.

Throws :

- `FileClosedException`

## 12.7 TextFile.close() : void

Ferme explicitement le fichier.

Règles :

- idempotent pour un fichier normal
- après fermeture, toute opération DOIT lever `FileClosedException`

Throws :

- `StandardStreamCloseException` si appelé sur `Io.stdin/stdout/stderr`

---

# 13. BinaryFile API

## 13.1 BinaryFile.read(size: int) : list<byte>

Lit `size` octets à partir de la position courante.

Règles :

- `size` DOIT être un int strictement positif (`>= 1`)
- EOF n’est pas une erreur

Retour :

- `list<byte>`
- `length == 0` indique EOF

Throws :

- `InvalidArgumentException`
- `FileClosedException`
- `ReadFailureException`

## 13.2 BinaryFile.write(bytes: list<byte>) : void

Écrit `bytes` à la position courante.

Règles :

- `bytes` DOIT être un `list<byte>`
- chaque élément DOIT être dans `0..255`

Atomicité et erreurs : cf. §14.

Throws :

- `InvalidArgumentException`
- `FileClosedException`
- `WriteFailureException`

## 13.3 BinaryFile.tell() : int

Retourne la position courante en octets.

Throws :

- `FileClosedException`
- `ReadFailureException`

## 13.4 BinaryFile.seek(pos: int) : void

Positionne le curseur à `pos` octets.

Règles :

- `pos` DOIT être un int `>= 0`
- si `pos > size()`, lever `InvalidArgumentException`

Throws :

- `InvalidArgumentException`
- `FileClosedException`
- `ReadFailureException`

## 13.5 BinaryFile.size() : int

Retourne la taille du fichier en octets.

Throws :

- `FileClosedException`
- `ReadFailureException`

## 13.6 BinaryFile.name() : string

Retourne le chemin/nom associé au handle.

Throws :

- `FileClosedException`

## 13.7 BinaryFile.close() : void

Identique à `TextFile.close()`.

---

# 14. Write Atomicity and Partial Write Semantics

Les opérations `write()` (texte et binaire) DOIVENT être logiquement atomiques au niveau du langage.

Règles normatives :

1. Si l’OS effectue une écriture partielle, l’implémentation DOIT boucler jusqu’à succès complet ou échec définitif.
2. En cas d’échec définitif, l’implémentation DOIT lever `WriteFailureException`.
3. Aucune écriture partielle ne DOIT être observable au niveau ProtoScript2.

## 14.1 État du curseur en cas d’échec

Si `write()` lève `WriteFailureException` :

- la position du curseur DOIT rester identique à la position avant l’appel.
- aucun avancement partiel n’est permis.

---

# 15. UTF-8 strict (mode texte)

Règles normatives en lecture texte :

- décodage UTF-8 obligatoire
- séquences UTF-8 invalides → `Utf8DecodeException`
- octet NUL interdit → `Utf8DecodeException`
- aucun traitement implicite de BOM

---

# 16. Index des glyphes (mode texte)

Les opérations `read(size)`, `tell()`, `seek(pos)` et `size()` travaillent en glyphes.

L’implémentation peut construire un index des positions de glyphes en parcourant le fichier (passe UTF-8).

Cela peut impliquer :

- un coût O(n) pour la première demande de `size()` ou `seek()`
- une invalidation/reconstruction après `write()`

Ce comportement est autorisé tant que la sémantique observable reste conforme.

---

# 17. Exemples

## 17.1 Texte séquentiel

```ps
TextFile f = Io.openText("notes.txt", "r");
while (true) {
  string s = f.read(128);
  if (s.length == 0) break;
  Io.print(s);
}
f.close();
```

## 17.2 Binaire séquentiel

```ps
BinaryFile f = Io.openBinary("data.bin", "r");
while (true) {
  list<byte> chunk = f.read(1024);
  if (chunk.length == 0) break;
  process(chunk);
}
f.close();
```

---

# 18. Non-objectifs

- I/O asynchrone
- mmap
- encodages alternatifs
- fermeture automatique

---

# 19. Exigences d’implémentation

- module natif C
- respect strict des règles de type
- tests couvrant :
  - texte, binaire
  - EOF via longueur nulle
  - erreurs typées
  - seek/tell/size
  - UTF-8 strict
  - atomicité write et échec d’écriture

---

# End of Specification

