![ProtoScript2](header.png)

# ProtoScript2 — Module Io (Specification v2)

## Statut

Cette spécification définit le module **Io** pour **ProtoScript2**.

Elle décrit **uniquement** le comportement attendu dans ProtoScript2. Aucune version antérieure, aucun autre langage et aucune spécification externe ne font autorité.

Cette spécification est **normative**.

---

## 1. Principes de conception

- I/O **synchrone** uniquement.
- Sémantique **explicite et déterministe**.
- Aucune conversion implicite texte/binaire.
- Durée de vie des ressources **manuelle** (close explicite).
- UTF-8 **strict** en lecture en mode texte.
- Aucun buffering ou flushing implicite.
- Les fichiers texte opèrent en **glyphes** (positions, tailles, read).
- Les fichiers binaires opèrent en **octets**.

---

## 2. Interface du module

```ps
import Io;
```

Le module expose :

- des fonctions globales,
- deux prototypes fermés **TextFile** et **BinaryFile**,
- une constante (`Io.EOL`),
- des flux standards.

Les prototypes **TextFile** et **BinaryFile** sont **fermés** : aucun champ public, uniquement les méthodes spécifiées ici.

---

## 3. Io.openText(path, mode) -> TextFile

Ouvre un fichier texte et retourne un handle **TextFile**.

### Paramètres

- `path` : `string`
- `mode` : `string`

### Modes (normatif)

| Mode | Signification |
| ---- | ------------- |
| `r`  | lecture |
| `w`  | ecriture (truncate) |
| `a`  | ecriture (append) |

Règles normatives :

- Le `mode` **DOIT** être exactement `r`, `w` ou `a`.
- Toute autre valeur **DOIT** lever une erreur runtime (mode invalide).

### Sémantique texte

- Lecture/écriture de `string`.
- Décodage UTF-8 strict en lecture (cf. §24).
- Aucune conversion implicite de fins de ligne.
- Aucun traitement implicite de BOM (un BOM est lu/écrit comme un glyphe normal).

### Erreurs

`Io.openText` **DOIT** lever une erreur runtime dans les cas suivants (liste non exhaustive) :

- fichier introuvable (selon mode)
- permissions insuffisantes
- mode invalide
- chemin invalide

---

## 4. Io.openBinary(path, mode) -> BinaryFile

Ouvre un fichier binaire et retourne un handle **BinaryFile**.

### Paramètres

- `path` : `string`
- `mode` : `string`

### Modes (normatif)

Identiques a `Io.openText` : `r`, `w`, `a` uniquement.

### Sémantique binaire

- Lecture/écriture de `list<byte>`.
- Aucun décodage.

### Erreurs

Identiques a `Io.openText`.

---

## 5. Io.tempPath() -> string

Retourne un chemin temporaire **unique** et **inexistant**.

### Comportement (normatif)

- **DOIT** retourner une `string`.
- Le chemin **DOIT** être valide pour le système courant.
- Le chemin **DOIT** être inexistant au moment du retour.
- La fonction **NE DOIT PAS** créer le fichier.
- Deux appels successifs **DOIVENT** retourner deux chemins distincts.
- **DOIT** utiliser le répertoire temporaire du système :
  - POSIX : `$TMPDIR` sinon `/tmp`
  - Windows : `%TEMP%`
- En cas d’échec système, **DOIT** lever une erreur runtime.
- **Aucune réservation durable** n’est effectuée.
- **Aucune suppression implicite** n’est effectuée.
- La fonction **NE protège PAS** contre une race condition externe.

---

## 6. Standard Streams (normatif)

Les flux standards suivants sont fournis comme **TextFile deja ouverts** :

- `Io.stdin` (lecture)
- `Io.stdout` (ecriture)
- `Io.stderr` (ecriture)

Règles normatives :

- Ces flux sont **en mode texte**.
- Ces flux **NE DOIVENT PAS** être fermés par le code utilisateur.
- Tout appel à `close()` sur l’un d’eux **DOIT** lever une erreur runtime.
- Leur durée de vie est celle du processus.

Exemples valides :

```ps
Io.stdout.write("Hello\n");
Io.stderr.write("Error: something went wrong\n");
```

---

## 7. Constantes

### 6.1 Io.EOL

```ps
Io.EOL == "\n"
```

Constante universelle de fin de ligne.

---

## 8. Io.print(value)

Ecrit `value` sur `Io.stdout` **sans ajouter de fin de ligne**.

Sémantique exacte (normative) :

```ps
Io.print(value)
```

est strictement equivalent a :

```ps
Io.stdout.write(String(value))
```

---

## 9. Io.printLine(value)

Ecrit `value` sur `Io.stdout` **et ajoute une fin de ligne**.

Sémantique exacte (normative) :

```ps
Io.printLine(value)
```

est strictement equivalent a :

```ps
Io.print(value)
Io.stdout.write(Io.EOL)
```

---

## 10. TextFile.read(size)

Lit `size` **glyphes** a partir de la position courante.

### Signature

```ps
textFile.read(size)
```

### Parametre `size` (normatif)

- `size` **DOIT** être un entier strictement positif (`size >= 1`).
- Si `size` n’est pas un entier, ou si `size <= 0`, l’appel **DOIT** lever une erreur runtime.

### Comportement

- La lecture démarre a la position courante (en **glyphes**).
- Le curseur avance du nombre de **glyphes** effectivement lus.
- L’EOF n’est pas une erreur.

### Valeur de retour

- `string`.
- Une longueur nulle (`length == 0`) **indique EOF**.

---

## 11. TextFile.write(text)

Ecrit `text` a la position courante.

### Signature

```ps
textFile.write(text)
```

### Parametre `text` (normatif)

- `text` **DOIT** être une `string`.

### Comportement

- Le curseur avance du nombre de **glyphes** ecrits.
- Aucune fin de ligne implicite n’est ajoutee.

Toute incoherence de type **DOIT** lever une erreur runtime.

---

## 12. TextFile.tell()

Retourne la position courante **en glyphes**.

```ps
textFile.tell() -> int
```

---

## 13. TextFile.seek(pos)

Positionne le curseur a la position `pos` **en glyphes**.

```ps
textFile.seek(pos)
```

Règles normatives :

- `pos` **DOIT** être un entier `>= 0`.
- Si `pos` est superieur a `size()`, l’appel **DOIT** lever une erreur runtime.

---

## 14. TextFile.size()

Retourne la taille du fichier en **glyphes**.

```ps
textFile.size() -> int
```

---

## 15. TextFile.name()

Retourne le chemin/nom associe au handle.

```ps
textFile.name() -> string
```

---

## 16. TextFile.close()

Ferme explicitement le fichier.

Règles normatives :

- L’operation est **idempotente** pour un fichier normal : fermer un fichier deja ferme ne doit pas echouer.
- Apres fermeture, toute operation (`read`, `write`, `tell`, `seek`, `size`, etc.) **DOIT** lever une erreur runtime.
- Appeler `close()` sur `Io.stdin`, `Io.stdout` ou `Io.stderr` **DOIT** lever une erreur runtime (cf. §6).

---

## 17. BinaryFile.read(size)

Lit `size` **octets** a partir de la position courante.

### Signature

```ps
binaryFile.read(size)
```

### Parametre `size` (normatif)

- `size` **DOIT** être un entier strictement positif (`size >= 1`).
- Si `size` n’est pas un entier, ou si `size <= 0`, l’appel **DOIT** lever une erreur runtime.

### Comportement

- La lecture démarre a la position courante (en **octets**).
- Le curseur avance du nombre d’**octets** effectivement lus.
- L’EOF n’est pas une erreur.

### Valeur de retour

- `list<byte>`.
- Une longueur nulle (`length == 0`) **indique EOF**.

---

## 18. BinaryFile.write(bytes)

Ecrit `bytes` a la position courante.

### Signature

```ps
binaryFile.write(bytes)
```

### Parametre `bytes` (normatif)

- `bytes` **DOIT** être un `list<byte>`.
- Chaque element **DOIT** être dans `0..255`.

### Comportement

- Le curseur avance du nombre d’**octets** ecrits.
- Aucune conversion implicite n’est effectuee.

Toute incoherence de type ou de contenu **DOIT** lever une erreur runtime.

---

## 19. BinaryFile.tell()

Retourne la position courante **en octets**.

```ps
binaryFile.tell() -> int
```

---

## 20. BinaryFile.seek(pos)

Positionne le curseur a la position `pos` **en octets**.

```ps
binaryFile.seek(pos)
```

Règles normatives :

- `pos` **DOIT** être un entier `>= 0`.
- Si `pos` est superieur a `size()`, l’appel **DOIT** lever une erreur runtime.

---

## 21. BinaryFile.size()

Retourne la taille du fichier en **octets**.

```ps
binaryFile.size() -> int
```

---

## 22. BinaryFile.name()

Retourne le chemin/nom associe au handle.

```ps
binaryFile.name() -> string
```

---

## 23. BinaryFile.close()

Identique a `TextFile.close()`.

---

## 24. UTF-8 strict (mode texte)

Regles normatives en lecture texte :

- Decodage UTF-8 obligatoire.
- Sequences UTF-8 invalides → erreur.
- Octet NUL interdit → erreur.
- Aucun traitement implicite de BOM.

---

## 25. Mode binaire

- Aucun decodage.
- Valeurs d’octet 0-255 autorisees.
- Representation : `list<byte>`.

---

## 26. Index des glyphes (mode texte)

Les operations `read(size)`, `tell()`, `seek(pos)` et `size()` travaillent en **glyphes**.

L’implementation **peut** construire un index des positions de glyphes en parcourant le fichier (passe UTF-8). Cela peut impliquer :

- un cout **O(n)** pour la premiere demande de `size()` ou `seek()`,
- une invalidation/reconstruction apres `write()`.

Ce comportement est **autorise** tant que la semantique observable reste conforme.

---

## 27. Exemples

### 26.1 Texte sequentiel

```ps
var f = Io.openText("notes.txt", "r");
while (true) {
  var s = f.read(128);
  if (s.length == 0) break;
  Io.print(s);
}
f.close();
```

### 26.2 Binaire sequentiel

```ps
var f = Io.openBinary("data.bin", "r");
while (true) {
  var chunk = f.read(1024);
  if (chunk.length == 0) break;
  process(chunk);
}
f.close();
```

---

## 28. Non-objectifs

- I/O asynchrone
- mmap
- encodages alternatifs
- fermeture automatique

---

## 29. Exigences d’implementation

- Module natif C.
- Respect strict des regles de type.
- Tests couvrant texte, binaire, EOF via longueur nulle, erreurs, seek/tell/size.

Fin de la specification.
