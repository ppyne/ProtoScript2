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
- UTF‑8 **strict** en lecture en mode texte.
- Aucun buffering ou flushing implicite.

---

## 2. Interface du module

```ps
import Io;
```

Le module expose :

- des fonctions globales,
- un type opaque **File**,
- une constante (`Io.EOL`),
- des flux standards.

---

## 3. Io.open(path, mode) -> File

Ouvre un fichier et retourne un handle **File**.

### Paramètres

- `path` : `string`
- `mode` : `string`

### Flags de mode (normatif)

| Flag | Signification |
| ---- | ------------- |
| `r`  | lecture |
| `w`  | écriture (truncate) |
| `a`  | écriture (append) |
| `b`  | mode binaire |

Règles normatives :

- Exactement **un** des flags `r`, `w`, `a` **DOIT** être présent.
- Le flag `b` est **optionnel** :
  - absent → **mode texte**
  - présent → **mode binaire**
- Les combinaisons valides sont donc : `r`, `w`, `a`, `rb`, `wb`, `ab`.
- Toute autre combinaison **DOIT** lever une erreur runtime (mode invalide).

### Sémantique texte vs binaire

- **Texte** : lecture/écriture de `string`. En lecture, décodage UTF‑8 strict (cf. §10).
- **Binaire** : lecture/écriture de `list<byte>` (aucun décodage).

### Erreurs

`Io.open` **DOIT** lever une erreur runtime dans les cas suivants (liste non exhaustive) :

- fichier introuvable (selon mode)
- permissions insuffisantes
- mode invalide
- chemin invalide

---

## 4. Standard Streams (normatif)

Les flux standards suivants sont fournis comme **handles File déjà ouverts** :

- `Io.stdin` (lecture)
- `Io.stdout` (écriture)
- `Io.stderr` (écriture)

Règles normatives :

- Ces flux sont **en mode texte** (comme si ouverts sans flag `b`).
- Ces flux **NE DOIVENT PAS** être fermés par le code utilisateur.
- Tout appel à `file.close()` sur l’un d’eux **DOIT lever une erreur runtime**.
- Leur durée de vie est celle du processus.

Les flux standards sont des **File ordinaires** et supportent les opérations définies sur `File`.

Exemples valides :

```ps
Io.stdout.write("Hello\n");
Io.stderr.write("Error: something went wrong\n");
```

---

## 5. Constantes

### 5.1 Io.EOL

```ps
Io.EOL == "\n"
```

Constante universelle de fin de ligne.

---

## 6. Io.print(value)

Écrit `value` sur `Io.stdout` **sans ajouter de fin de ligne**.

Sémantique exacte (normative) :

```ps
Io.print(value)
```

est strictement équivalent à :

```ps
Io.stdout.write(String(value))
```

---

## 6.1 Io.printLine(value)

Écrit `value` sur `Io.stdout` **et ajoute une fin de ligne**.

Sémantique exacte (normative) :

```ps
Io.printLine(value)
```

est strictement équivalent à :

```ps
Io.print(value)
Io.stdout.write(Io.EOL)
```

---

## 7. File.read([size])

Lit des données à partir de la position courante.

### Principe fondamental (normatif)

- **En l’absence du flag `b`, toute lecture est en mode texte et retourne une `string`.**
- **Le mode binaire (`list<byte>`) n’est actif que si et seulement si le fichier a été ouvert avec le flag `b`.**
- Il n’existe **aucune conversion implicite** entre texte et binaire.
- En mode texte, il n’y a **aucune traduction** de fin de ligne (pas de conversion `\r\n` → `\n`).

### Signatures

```ps
file.read()
file.read(size)
```

### Paramètre `size` (normatif)

- `size` **DOIT** être un entier strictement positif (`size >= 1`).
- Si `size` n’est pas un entier, ou si `size <= 0`, l’appel **DOIT** lever une erreur runtime.

### Comportement

- La lecture démarre à la position courante.
- Le curseur avance du nombre d’octets effectivement lus.
- L’EOF n’est pas une erreur.

### Valeurs de retour

| Mode    | read()        | read(size)        |
| ------- | ------------- | ----------------- |
| texte   | `string`      | `string`          |
| binaire | `list<byte>`  | `list<byte>`      |

### EOF (normatif)

- `file.read()` (sans `size`) **lit jusqu’à EOF** et retourne une valeur éventuellement **vide**.
- `file.read(size)` retourne **toujours** une valeur de même type que le mode :
  - `string` en mode texte,
  - `list<byte>` en mode binaire.
- **Une longueur nulle** (`length == 0`) **indique EOF**.
- L’EOF n’est pas une erreur.

---

## 8. File.write(data)

Écrit des données à la position courante.

### Principe fondamental (normatif)

- **En l’absence du flag `b`, `file.write` attend une `string`.**
- **En présence du flag `b`, `file.write` attend un `list<byte>`.**
- Aucune conversion implicite n’est effectuée.

### Types acceptés (normatif)

| Mode    | Type attendu |
| ------- | ------------ |
| texte   | `string` |
| binaire | `list<byte>` |

Contraintes supplémentaires en mode binaire (normatif) :

- Le `list<byte>` **DOIT** contenir uniquement des entiers `0..255`.
- Toute valeur hors plage, ou non entière, **DOIT** lever une erreur runtime.

### Comportement

- Le curseur avance du nombre d’octets effectivement écrits.
- Aucune fin de ligne implicite n’est ajoutée.

Toute incohérence de type ou de contenu **DOIT** lever une erreur runtime.

---

## 9. File.close()

Ferme explicitement le fichier.

Règles normatives :

- L’opération est **idempotente** pour un fichier normal : fermer un fichier déjà fermé ne doit pas échouer.
- Après fermeture, toute opération (`read`, `write`, etc.) **DOIT lever une erreur runtime**.
- Appeler `close()` sur `Io.stdin`, `Io.stdout` ou `Io.stderr` **DOIT lever une erreur runtime** (cf. §4).

---

## 10. UTF‑8 strict (mode texte)

Règles normatives en lecture texte :

- Décodage UTF‑8 obligatoire.
- BOM UTF‑8 accepté **uniquement** au début du fichier (ignoré).
- BOM ailleurs → erreur.
- Séquences UTF‑8 invalides → erreur.
- Octet NUL interdit → erreur.

---

## 11. Mode binaire

- Aucun décodage.
- Valeurs d’octet 0–255 autorisées.
- Représentation : `list<byte>`.

---

## 12. Exemple binaire séquentiel

```ps
var f = Io.open("data.bin", "rb");
while (true) {
  var chunk = f.read(1024);
  if (chunk.length == 0) break;
  process(chunk);
}
f.close();
```

---

## 13. Gestion des ressources

Aucune fermeture automatique.

Usage recommandé :

```ps
var f = Io.open("notes.txt", "w");
try {
  f.write("hello" + Io.EOL);
} finally {
  f.close();
}
```

---

## 14. Non‑objectifs

- I/O asynchrone
- seek / tell
- mmap
- encodages alternatifs
- fermeture automatique

---

## 15. Exigences d’implémentation

- Module natif C.
- Respect strict des règles de type.
- Tests couvrant texte, binaire, EOF via longueur nulle, erreurs.

Fin de la spécification.
