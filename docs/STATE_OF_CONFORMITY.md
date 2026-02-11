Feature/Concept: Résolution des modules (nommés, search_paths, runtime)  
Spec Reference: `SPECIFICATION.md` §20.2–20.6  
Front-end: implemented  
CLI (ps run): partial  
C backend: implemented  
IR / VM: implemented  
Node runtime: partial  
Tests: partial  
Comment: frontends utilisent `modules/registry.json` + `search_paths`. Runtime C charge dynlibs via `PS_MODULE_PATH`/`./modules`/`./lib` (ignore `search_paths`). Runtime Node ne charge pas de modules dynamiques externes.  
Suggested minimal test case:
```c
// registry.json: search_paths = ["./vendor"]
import vendor.mod;
function main() : void { vendor.mod.ping(); }
```

Feature/Concept: Préprocesseur (mcpp) + mapping #line  
Spec Reference: non spécifié dans spec/manuel  
Front-end: partial  
CLI (ps run): implemented (C)  
C backend: implemented  
IR / VM: implemented  
Node runtime: missing  
Tests: absent  
Comment: pipeline C applique mcpp + table de correspondance; pipeline Node ne préprocesse pas.  
Suggested minimal test case:
```c
#define X 1
function main() : void { int x = X; }
```

Feature/Concept: Constructions syntaxiques (grammaire complète)  
Spec Reference: `SPECIFICATION.md` §20 (Syntaxe normative)  
Front-end: implemented  
CLI (ps run): implemented  
C backend: implemented  
IR / VM: implemented  
Node runtime: implemented  
Tests: partial  
Comment: Implémenté pour ImportStmt, PrototypeDecl, FunctionDecl, VarDecl, AssignStmt, If/Else, While, DoWhile, For, ForIn/ForOf, Switch, Try/Catch/Finally, Break/Continue/Return/Throw, Block, et expressions (littéraux, ident, call, member, index, unary, binary, ternary, cast, list/map literals, parenthèses). Couverture tests partielle selon les variantes.  
Suggested minimal test case:
```c
function main() : void {
    int x = 1;
    if (x > 0) { x += 1; } else { x -= 1; }
}
```
