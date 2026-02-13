# ProtoScript2 — Runtime Memory Model

Ce document décrit la propriété mémoire et le cycle de vie des structures runtime et IR, sur la base du code C actuel.  
Les références de fonctions sont indiquées sous la forme `fichier:fonction`.

## Règles globales

- Toutes les valeurs runtime (`PS_Value`) sont **référencées** (refcount) et doivent être libérées via `ps_value_release` (`c/runtime/ps_value.c:ps_value_release`).
- L’IR est un graphe séparé, chargé en mémoire via `ps_ir_load_json` et libéré **explicitement** par `ps_ir_free` (`c/runtime/ps_vm.c:ps_ir_free`).
- Il n’existe pas de GC. Le propriétaire doit libérer explicitement toutes les ressources.
- Aucune interning globale des chaînes n’est utilisée côté runtime (allocation par valeur).

## Embedder responsibilities (MUST)

- Appeler `ps_ir_free` pour **chaque** module IR chargé.
- Appeler `ps_ctx_destroy` pour **chaque** contexte créé (libère le registre de modules et ferme les dynlibs).
- Appeler `ps_value_release` pour **toute** valeur runtime retenue (retained).

## Tableau de propriété (résumé)

| Structure | Alloué par | Propriétaire | Partagé ? | Dupliqué par clone ? | Libéré par |
| --- | --- | --- | --- | --- | --- |
| `PS_IR_Module` (IR complet) | `c/runtime/ps_vm.c:ps_ir_load_json` | l’appelant (CLI ou embedder) | Oui (réutilisé par runs du même module) | Non | `c/runtime/ps_vm.c:ps_ir_free` |
| Prototypes IR (`PS_IR_Proto`) | `ps_ir_load_json` | `PS_IR_Module` | Oui (toutes exec VM) | Non | `ps_ir_free` |
| Groupes IR (`PS_IR_Group`) | `ps_ir_load_json` | `PS_IR_Module` | Oui (toutes exec VM) | Non | `ps_ir_free` |
| Fonctions/blocks/instructions IR | `ps_ir_load_json` | `PS_IR_Module` | Oui | Non | `ps_ir_free` |
| `PS_Context` | `c/runtime/ps_heap.c:ps_ctx_create` | Appelant | Non | N/A | `c/runtime/ps_heap.c:ps_ctx_destroy` |
| Registre de modules (`PS_ModuleRecord[]`) | `c/runtime/ps_modules.c:ensure_module_cap` | `PS_Context` | Oui (dans le contexte) | Non | `ps_ctx_destroy` |
| `PS_Value` (toutes valeurs runtime) | `c/runtime/ps_value.c:ps_value_alloc` | Refcount | Oui | N/A | `ps_value_release` |
| `string` (`PS_V_STRING`) | `c/runtime/ps_string.c:ps_string_from_utf8` | valeur | Non | N/A | `ps_value_free` |
| `bytes` (`PS_V_BYTES`) | `c/runtime/ps_api.c:ps_make_bytes` | valeur | Non | N/A | `ps_value_free` |
| `list<T>` (`PS_V_LIST`) | `c/runtime/ps_list.c:ps_list_new` | valeur | Non | N/A | `ps_value_free` |
| `map<K,V>` (`PS_V_MAP`) | `c/runtime/ps_map.c:ps_map_new` | valeur | Non | N/A | `ps_value_free` |
| `object` (`PS_V_OBJECT`) | `c/runtime/ps_object.c:ps_object_new` | valeur | Non | N/A | `ps_value_free` |
| `view<T>` (`PS_V_VIEW`) | `c/runtime/ps_vm.c` (op `make_view`) | valeur | Oui (référence source) | N/A | `ps_value_free` |
| `iter` (`PS_V_ITER`) | `c/runtime/ps_vm.c` | valeur | Oui (référence source) | N/A | `ps_value_free` |
| `file` (`PS_V_FILE`) | `c/runtime/ps_api.c:ps_make_file` | valeur | Non | N/A | `ps_value_free` |
| `exception` (`PS_V_EXCEPTION`) | `c/runtime/ps_api.c:ps_throw_exception` et `c/runtime/ps_vm.c:make_exception` | valeur | Non | N/A | `ps_value_free` |

## Détails et invariants vérifiés

### IR (modules, prototypes, groupes, instructions)
- **Allocation**: `c/runtime/ps_vm.c:ps_ir_load_json` alloue `PS_IR_Module`, ses `IRFunction`, `IRBlock`, `IRInstr`, `PS_IR_Proto`, `PS_IR_Group` et leurs chaînes associées.
- **Partage**: une instance IR est partagée par toutes les exécutions de `ps_vm_run_main` pour ce module.
- **Libération**: `c/runtime/ps_vm.c:ps_ir_free` libère **toutes** les structures IR (y compris prototypes et groupes).
- **Duplication**: aucune duplication par clone ni par frame; l’IR est uniquement par module chargé.

### Prototypes et instances
- **Descripteurs**: les prototypes sont des métadonnées **IR** (`PS_IR_Proto`) et ne sont pas copiés par clone.
- **Instances**: un clone crée un objet runtime (`PS_V_OBJECT`) et lui assigne un `proto_name` (`c/runtime/ps_vm.c` op `make_object` + `ps_object_set_proto_name_internal`).
- **Champ `proto_name`**: stocké par instance (duplication **volontaire** d’une chaîne), pas de table globale runtime.

### `PS_Value` et sous-structures
- **Refcount**: `c/runtime/ps_value.c:ps_value_alloc / ps_value_release / ps_value_free`.
- **string/bytes**: buffer alloué par valeur, libéré dans `ps_value_free`.
- **list<T>**: `items` alloué/agrandi via `c/runtime/ps_list.c:ensure_cap`, libéré dans `ps_value_free` via `ps_list_free`.
- **map<K,V>**: `keys/values/used/order` alloués via `c/runtime/ps_map.c:ensure_cap` et `ensure_order_cap`, libérés dans `ps_map_free`.
- **object**: tables `keys/values/used` allouées via `c/runtime/ps_object.c:ensure_cap`, chaînes de clés allouées en `ps_object_set_str_internal`, libérées dans `ps_object_free`.

### `view<T>` / `slice<T>` / itérateurs
- **`view<T>`** est une valeur runtime (`PS_V_VIEW`) qui **retient** sa source via refcount (`ps_value_retain` dans `c/runtime/ps_vm.c` op `make_view`).
- **Invalidation**: un view sur une `list` est invalidé si la version de la liste change (`c/runtime/ps_vm.c:view_is_valid`).
- **Libération**: `ps_value_free` libère `type_name` et release la source (pas de copie de données).
- **Invariants**: un view ne possède jamais la mémoire de la collection; il retient simplement la source.

### Exceptions
- **Allocation**: `c/runtime/ps_api.c:ps_throw_exception` et `c/runtime/ps_vm.c:make_exception`.
- **Champs**: `fields` est un objet runtime; `file/message/cause/code/category` sont des `PS_Value`.
- **Libération**: `ps_value_free` libère toutes les références détenues par `PS_V_EXCEPTION`.

### Modules natifs
- **Registre**: `ctx->modules` est alloué via `realloc` dans `c/runtime/ps_modules.c:ensure_module_cap`.
- **Libération**: `ps_ctx_destroy` ferme les bibliothèques dynamiques (`ps_dynlib_close`) et libère le tableau.
- **Invariants**: aucune API d’unload explicite; l’embedder doit appeler `ps_ctx_destroy`.

## Déterminisme et non-rétention

- Aucun cache runtime dépendant d’adresses mémoire n’est utilisé.
- Les sorties debug/test ne doivent pas contenir d’adresses mémoire, sauf tests de robustesse explicitement instrumentés.

## TODO / incertitudes

- Aucun cache de méthodes ou d’interning n’est présent actuellement.  
  Si un cache est introduit, il doit être documenté ici et testé pour éviter la duplication par clone/frame.
