# Ecrire un module natif ProtoScript (C)

Ce document explique comment ecrire, compiler, charger et tester un module natif ProtoScript en C, en utilisant uniquement l'ABI publique (`ps_api.h`). Il est autonome et ne suppose aucune lecture du code source interne.

## 1) Introduction

### Qu'est-ce qu'un module natif ProtoScript ?
Un module natif est une bibliotheque partagee (POSIX) qui expose des fonctions C et les enregistre comme fonctions ProtoScript. Le runtime C charge ces modules dynamiquement et les appelle via l'ABI publique.

### Quand utiliser un module natif ?
Utilisez un module natif lorsque :
- vous devez acceder a des bibliotheques C existantes,
- vous avez besoin de performances ou d'I/O bas niveau,
- vous voulez exposer une API stable a plusieurs programmes ProtoScript.

Evitez un module natif quand une fonction ProtoScript suffit (le module ajoute des contraintes de build et de distribution).

### Modele d'execution
- Chargement dynamique POSIX uniquement (macOS/Linux).
- Resolution des imports a la compilation via `registry.json`.
- A l'execution, le runtime charge `psmod_<name>.(so|dylib)` et appelle `ps_module_init`.

## 2) Prerequis

- Systeme POSIX (macOS, Linux).
- Un compilateur C (cc, clang ou gcc).
- L'en-tete public : `include/ps/ps_api.h`.
- L'ABI versionnee via `PS_API_VERSION`.

## 3) Structure minimale d'un module

### Convention de nommage
Nom logique du module : `test.simple`

Nom du fichier charge :
- `psmod_test_simple.so` (Linux)
- `psmod_test_simple.dylib` (macOS)

Les points du nom logique sont remplaces par `_`.

### Symbole requis
Le module doit exposer exactement ce symbole :

```c
PS_Status ps_module_init(PS_Context* ctx, PS_Module* out);
```

### Role de PS_Context et PS_Module
- `PS_Context* ctx` : contexte runtime, pour creer des valeurs, lever des erreurs, etc.
- `PS_Module* out` : structure a remplir pour declarer le module et ses fonctions.

## 4) Exemple minimal complet

### Objectif
Expose `test.simple.add(a: int, b: int) -> int`.

### Code C complet

```c
#include "ps/ps_api.h"

static PS_Status mod_add(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  int64_t a = ps_as_int(argv[0]);
  int64_t b = ps_as_int(argv[1]);
  PS_Value *v = ps_make_int(ctx, a + b);
  if (!v) return PS_ERR;
  *out = v; // ownership: caller (runtime) prend ce handle
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "add", .fn = mod_add, .arity = 2, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "test.simple";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
```

### Compilation

Linux :
```sh
cc -std=c11 -O2 -fPIC -shared -I/path/to/include \
  test_simple.c -o psmod_test_simple.so
```

macOS :
```sh
cc -std=c11 -O2 -fPIC -dynamiclib -I/path/to/include \
  test_simple.c -o psmod_test_simple.dylib
```

Placez le fichier dans un dossier chargeable par le runtime (voir section 7).

### Template "module skeleton" (copier/coller)

```c
#include "ps/ps_api.h"

// TODO: remplacer par votre logique.
static PS_Status mod_fn(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  // Exemple: retourne 0
  PS_Value *v = ps_make_int(ctx, 0);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "fn", .fn = mod_fn, .arity = 0, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "my.module";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
```

## 5) API C (ps_api.h)

### Types opaques
- `PS_Value` : valeur runtime opaque.
- `PS_Context` : contexte runtime opaque.

Vous ne devez jamais acceder a des champs internes.

### Creation de valeurs
- `ps_make_int`, `ps_make_bool`, `ps_make_float`, `ps_make_byte`, `ps_make_glyph`
- `ps_make_string_utf8`, `ps_make_bytes`
- `ps_make_list`, `ps_make_object`

Toutes ces fonctions retournent un handle **possede par l'appelant** (refcount +1).

### Inspection / conversion
- `ps_as_int`, `ps_as_bool`, `ps_as_float`, `ps_as_byte`, `ps_as_glyph`
- `ps_string_ptr`, `ps_string_len`
- `ps_bytes_ptr`, `ps_bytes_len`
- `ps_typeof`

Les acces **ne transferent pas** l'ownership.

### Gestion des handles (ownership)
Regles obligatoires :
- **Toute valeur retournee par `ps_make_*` est possedee par l'appelant.**
- **Si vous stockez un `PS_Value*` au-dela de la portee locale, vous devez `ps_value_retain`.**
- **Quand vous n'en avez plus besoin, appelez `ps_value_release`.**
- `ps_handle_push(ctx, v)` garde un handle vivant (refcount +1).
- `ps_handle_pop(ctx)` retire la racine (refcount -1).

### Erreurs natives
- Utilisez `ps_throw(ctx, code, "message")`.
- Retournez ensuite `PS_ERR`.
- L'erreur est propagee vers ProtoScript et peut etre capturee par `try/catch`.

Exemple d'erreur :
```c
ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
return PS_ERR;
```

## 6) Gestion des strings (UTF-8 strict)

### UTF-8 strict
Toutes les strings runtime sont en UTF-8 valide. Toute sequence invalide est rejetee.

- `ps_make_string_utf8` valide l'UTF-8.
- `ps_string_to_utf8_bytes` retourne une liste de bytes.
- `ps_bytes_to_utf8_string` valide strictement l'UTF-8 et echoue si invalide.

Erreurs possibles :
- `PS_ERR_UTF8` si l'encodage est invalide.

## 7) Import cote ProtoScript

Exemple d'import :

```ps
import test.simple.{add};

function main() : void {
    Sys.print(add(2, 3).toString());
}
```

### Resolution statique via registry.json
Le compilateur utilise `modules/registry.json` comme source de verite pour :
- la resolution des imports,
- l'arite et les types declares.

Exemple minimal :
```json
{
  "modules": [
    {
      "name": "test.simple",
      "functions": [
        { "name": "add", "ret": "int", "params": ["int", "int"] }
      ]
    }
  ]
}
```

### Chargement runtime
Le runtime charge le module a l'execution via :
- `PS_MODULE_PATH` (liste de dossiers, separes par `:`), puis
- `./modules`, puis `./lib`.

### Exemple de build + PS_MODULE_PATH

```sh
# build
cc -std=c11 -O2 -fPIC -shared -I/path/to/include \
  test_simple.c -o /abs/path/to/modules/psmod_test_simple.so

# execution (macOS/Linux)
export PS_MODULE_PATH=/abs/path/to/modules
./c/ps run examples/use_module.pts
```

Erreurs d'import possibles :
- module introuvable
- symbole manquant
- ABI incompatible

## 8) Tests et debug

### Tester un module
- `ps run <file>` execute le programme.
- `ps check <file>` verifie uniquement parse + typecheck.

### Erreurs frequentes
- **Module introuvable** : chemin incorrect, nom de fichier non conforme.
- **Symbole manquant** : `ps_module_init` absent ou nom de fonction incorrect.
- **ABI incompatible** : `PS_API_VERSION` different.
- **Erreur native** : `ps_throw` appelle avec `PS_ERR_*`.

## 9) Limitations actuelles (obligatoire)

- Pas de GC (refcount uniquement).
- Pas de cycles references.
- `catch` est catch-all (pas de dispatch par type).
- La variable `e` dans `catch` est une **string** (message uniquement).
- Les codes `Rxxxx` ne sont pas exposes dans la CLI C.

## 10) Bonnes pratiques

- Toujours expliciter l'ownership des `PS_Value*`.
- Ne jamais conserver un `PS_Value*` sans `ps_value_retain`.
- Liberer systematiquement via `ps_value_release`.
- Utiliser `ps_throw` pour toute erreur detectee.
- Versionner vos modules (nom logique stable + compatibilite ABI).
