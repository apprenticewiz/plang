# EP Modules in plang

ISO 10206 Extended Pascal adds a module system for organizing code into reusable,
separately-named compilation units. plang supports two compilation models:

- **Single-file**: module definitions and the program live in one `.pas` file,
  compiled in one invocation.
- **Separate compilation**: modules are compiled independently to `.o` object files
  and `.pmi` interface files; programs import from `.pmi` files and link against
  the corresponding `.o` files.

---

## Syntax Overview

### Module body

A module body declares procedures, functions, types, and variables that it
makes available to programs and other modules.

```pascal
module Arith;
  import Math only sqrt;     { optional imports }

  function Square(x: real): real;
  begin Square := x * x end;

  var CallCount: integer;

  to begin do CallCount := 0;   { runs before the program body }
  to end do writeln('done');    { runs after the program body }
end.
```

### Module interface (heading)

A module interface declares the *public surface* of a module without providing
implementations. It is the EP equivalent of a C header file: an export list
naming what leaves the module, followed by the declarations of those names,
with procedures and functions written as headings alone.

```pascal
module Arith interface;

export Arith = (Square, protected CallCount);

var CallCount: integer;
function Square(x: real): real;

end.
```

The identifier before the `=` names the interface. ISO 10206 lets a module
offer several, and an importer name the one it wants; plang gives a module a
single interface, so the name is recorded but not otherwise used, and several
export parts describe the same one.

An interface may appear before the corresponding body in the same file. When the
compiler sees both, the body satisfies the interface's exported declarations.
A module with no interface exports everything it declares.

### Implementation modules

The block that implements an interface is written either as its own module
declaration, marked with the `implementation` directive, or directly after the
interface it belongs to. Both spellings give the block every declaration the
interface makes — exported or not — so it need not repeat them, and a routine
whose heading is in the interface is written there as its name alone:

```pascal
module Arith interface;
export Arith = (Square, CallCount);
var CallCount: integer;
function Square(x: real): real;
end;                             { a ';' here says the block follows }

function Square;                 { the heading was given above }
begin CallCount := CallCount + 1; Square := x * x end;
to begin do CallCount := 0;
end.
```

Written apart, the same module is:

```pascal
module Arith interface;
  ...
end.

module Arith implementation;
  function Square(x: real): real;
  begin Square := x * x end;
end.
```

`module Arith;` without the directive is read the same way, which is how
plang's modules were written before the directive was accepted.

The older plang spelling, in which the `export` section holds the declarations
themselves rather than a list of their names, is still read:

```pascal
module Arith interface;
  export
    function Square(x: real): real;
end.
```

### Module parameters

A module may declare file parameters, similar to a program heading:

```pascal
module Logger(output);
  procedure Log(msg: string);
  begin writeln(output, msg) end;
end.
```

### Program using a module

A program imports a module by name. Imported symbols become directly visible
inside the program block.

```pascal
program p;
  import Arith;
  import Math only sqrt, pi;   { import only selected names }
begin
  writeln(Square(3.0));        { calls Arith.Square }
  writeln(sqrt(2.0))           { calls Math.sqrt }
end.
```

---

## Import Variants

| Syntax | Effect |
|--------|--------|
| `import M;` | All exported names of M become visible. |
| `import M only (f, g);` | Only `f` and `g` are imported from M. |
| `import M qualified;` | Imports all names; they must be accessed as `M.name`. |
| `import M (f => g);` | Imports everything, but `f` is visible as `g`. |
| `import M only (f => g);` | Imports `f` alone, visible as `g`. |
| `import M qualified (f => g);` | As above, reached as `M.g`. |

`qualified` and the name list may be written in either order. A name list
without parentheses — `import M only f, g;` — is the older plang spelling and
still reads. A name in the list that the interface does not export is an error,
as is importing a module that is neither defined in this file nor has an
interface file on the search path.

Multiple imports may appear in one clause or in separate `import` statements,
separated by semicolons.

---

## Export Variants

An export clause may rename what it exports, so that callers see a name the
module does not use internally, and may mark a variable `protected`, which
leaves it readable where it is imported but not assignable:

```pascal
module Arith interface;

export Arith = (InternalSqrt => Sqrt, protected Count, Color, red..green);

type Color = (red, orange, yellow, green, blue);
var Count: integer;
function InternalSqrt(x: real): real;

end.
```

`first..last` is an export range: it exports the constants of one enumerated
type from `first` through `last` and no others, so `blue` above stays inside
the module while `red`, `orange`, `yellow` and `green` leave it.

Renaming survives separate compilation, and renaming on import stacks on top of
it: with the interface above, `import Arith (Sqrt => Root)` makes the function
`Root` here, `Sqrt` to anyone who imports it plainly, and `InternalSqrt` in the
object file.

---

## Initialization and Finalization

A module body may contain at most one `to begin do` clause and one `to end do`
clause, placed after all declarations and before the closing `end.`:

```pascal
module Logger;
  var LogFile: text;

  to begin do begin
    rewrite(LogFile, '/tmp/app.log');
    writeln(LogFile, 'started')
  end;

  to end do begin
    writeln(LogFile, 'finished');
    close(LogFile)
  end;
end.
```

**Ordering guarantees:**

- All module initialization bodies (`to begin do`) run **before** the program's
  `begin ... end` body, in the order the modules appear in the source file.
- All module finalization bodies (`to end do`) run **after** the program body
  returns, in reverse order.

---

## Single-File Multi-Unit Compilation

The simplest way to use modules is to place module definitions and the program in
one `.pas` file, compiled with `-std=iso10206`:

```pascal
{ arith.pas }
module Arith;
  function Double(x: integer): integer;
  begin Double := x * 2 end;
end.

program p;
  import Arith;
begin
  writeln(Double(21))   { prints 42 }
end.
```

```bash
plang -std=iso10206 arith.pas -o arith
./arith
```

---

## Separate Compilation

For larger projects, modules can be compiled independently and linked together.

### Step 1 — Compile the module

A module-only `.pas` file (no `program` keyword) produces both an object file and
a `.pmi` (Pascal Module Interface) file:

```bash
plang -std=iso10206 -c Arith.pas
```

This writes:
- `Arith.o` — compiled object code for the module body
- `Arith.pmi` — Pascal interface declarations that importers read at compile time

The `.pmi` file is the module's interface written out as Extended Pascal, which
is the same thing an interface in the source is, so importing across separate
compilation and importing within one file are the same act:

```pascal
{ plang module interface: Arith }
module Arith interface;
export Arith = (Double => Twice, protected CallCount);
var CallCount: integer;
function Double(x: integer): integer;
end.
```

Because the export list travels with it, renaming and `protected` mean the same
thing to an importer whether or not the module was compiled separately.

### Step 2 — Compile the program

Point the compiler at the directory containing the `.pmi` file with `-I`:

```bash
plang -std=iso10206 -I. main.pas Arith.o -o app
```

When the compiler sees `import Arith`, it searches the `-I` paths for `Arith.pmi`,
loads the declarations from it, and type-checks the program against them. The
`Arith.o` object file is passed directly to the linker.

### Multi-file driver shorthand

The driver also accepts multiple `.pas` source files in one invocation. It
compiles each one in order (producing `.pmi` files alongside the objects) and
links everything at the end:

```bash
plang -std=iso10206 main.pas Arith.pas -o app
```

Each extra `.pas` file's directory is automatically added to the module search
path, so `import Arith` in `main.pas` resolves to the `Arith.pmi` that was just
written beside `Arith.pas`.

### Complete example

**`Arith.pas`**:
```pascal
module Arith;
  function Double(x: integer): integer;
  begin Double := x * 2 end;
  var CallCount: integer;
  to begin do CallCount := 0;
end.
```

**`main.pas`**:
```pascal
program p;
  import Arith;
begin
  writeln(Double(21));      { prints 42 }
  writeln(CallCount)        { prints 0 }
end.
```

**Build**:
```bash
# One-shot (recommended):
plang -std=iso10206 main.pas Arith.pas -o app

# Or manually:
plang -std=iso10206 -c Arith.pas          # → Arith.o, Arith.pmi
plang -std=iso10206 -I. main.pas Arith.o -o app
```

### Module search path

The `-I` flag adds a directory to the module search path. Multiple `-I` flags
are allowed; they are searched left-to-right. If no `-I` is given, the current
directory is tried as a final fallback.

```bash
plang -std=iso10206 -I./modules -I/usr/local/pascal/lib main.pas -o app
```

---

## StandardInput and StandardOutput

EP §6.11.4.2 defines two required module interfaces: `StandardInput` and
`StandardOutput`. These provide the standard text-file symbols (`input`, `output`,
`read`, `readln`, `write`, `writeln`). In plang these names are always in scope as
built-ins, so importing them is a no-op — valid syntax, no effect:

```pascal
import StandardInput;
import StandardOutput;
```

---

## How It Works Internally

### Parse phase

When the parser (in EP mode) sees `module` as the first keyword of a file, it
enters multi-unit mode via `parseMultiUnitFile()`. It repeatedly calls
`parseModuleNode()` until it sees `program`, which it parses normally. The
resulting `ProgramNode` carries the module definitions in its `OwnedModules` /
`Modules` fields alongside its own `Imports` list.

### Sema phase

`Sema::check()` processes modules before the program:

1. **Interface modules** (`IsInterface = true`): `processModuleInterface()` walks
   the `Exports` list and registers placeholder symbols in the
   `ModuleExports_[name]` table.
2. **Body modules** (`IsInterface = false`): `processModuleBody()` runs
   `checkBlock()` on the module's block, then populates `ModuleExports_[name]`
   with the resolved types, procedures, functions, and variables declared in that
   block.
3. **Program imports**: `processImports()` looks up each imported module in
   `ModuleExports_`. If the module is not found there, it searches
   `LangOptions::ModuleSearchPaths` (then the current directory) for a
   `ModuleName.pmi` file. When found, `loadPMI()` scans and parses the file,
   runs `checkBlock()` on its declarations in a temporary scope, collects the
   resulting symbols into `ModuleExports_[name]`, then proceeds with the normal
   import path. The (optionally filtered or renamed) symbols are injected into the
   program's current scope.

### Codegen phase

`Codegen::emit()` processes module bodies before the program:

1. Module globals and procedures are emitted into the same LLVM module as the
   program. A module is an outer naming scope in the same sense a procedure is,
   so what it declares is mangled with its lowercased name the way a nested
   procedure is mangled with its enclosing one: `pas_<module>$<proc>` and
   `pasg_<module>$<var>`, and a procedure nested inside one of those becomes
   `pas_<module>$<proc>$<inner>`. Two modules may each export an `f`, and
   without this both want the symbol `pas_f`.

   `pas_` and `pasg_` are what everything the *source* names is built from, and
   they are not what the runtime's own entry points use, which is `plang_`.
   Until 0.1.3 both halves of the link shared `plang_`, so a program declaring
   its own `close`, `round`, `page` or `halt` — thirty-three names collided,
   twenty-four of them required identifiers ISO §6.2.2.10 entitles a program to
   redeclare — asked the linker for a symbol the runtime had already defined.

   The `$` joining a scope to what it declares is there because no Pascal
   identifier can contain one, so a mangled name separates into its parts
   exactly one way. Joined with `__`, as it was before 0.1.3, it did not: §6.1.3
   allows an underscore inside an identifier, so a module `a` exporting `b` and
   a top-level `a__b` were both `pas_a__b`, and every call reached whichever
   was emitted first.

   Each body is also emitted in its own codegen scope, so an unqualified name
   in the program reaches an imported declaration through the import clauses
   rather than through whichever module happened to be emitted last. Sema
   supplies the mapping — see `Sema::importOwners()` and `ImportedName` — since
   a reference records only the identifier, and which module declares it
   depends on the imports of the unit being emitted.
2. `emitModuleInitFn()` generates a `void __plang_init_<name>()` for **every**
   module, whether or not it has a `to begin do` — an importer has to be able to
   call it without knowing. It guards itself on a `__plang_initdone_<name>` flag,
   calls the initialiser of each module it imports, and then runs the
   `to begin do` statement.

   `main()` calls the initialiser of every module in its own compilation unit
   and of every module its program imports. The recursion supplies the rest:
   a module comes up after everything it imports (EP §6.11.2), exactly once
   however many paths reach it, and the same either side of a `-c` boundary.
   The program cannot work the order out itself, because it cannot see what a
   separately compiled module imports in turn.
3. If a module has a `to end do` statement, a `void __plang_fini_<name>()` is
   generated, and its initialiser ends by handing it to the runtime
   (`plang_module_final_push`). `main()` ends with `plang_module_finals_run()`,
   which runs them most-recently-initialized first. Taking the order from what
   actually happened is what makes it right across compilation units.

For **separately compiled modules** (imported via `.pmi`), the program does not
see the module's body in the current compilation unit. References to imported
procedures and variables are emitted as external LLVM declarations
(`declare`/`@pasg_<module>__<name> = external global`), under the same mangling the
module's own object file used. The linker resolves them against that `.o`.

### PMI file format

A `.pmi` file is a module interface:

```pascal
{ plang module interface: Arith }
module Arith interface;
export Arith = (Double, CallCount, Row);
const MaxRows = 5;
type Row = array[1..MaxRows] of real;
var CallCount: integer;
function Double(x: integer): integer;
end.
```

Constants come before the types, which are often written in terms of them, and
a structured constant comes after them, since it names the type it is a value
of. A constant is written whether or not the export list names it: what the
list leaves out stays unimportable, but a bound naming a constant the file
never declares would leave the type unreadable.

A type is written as it was declared, down to what an importer would otherwise
have to guess: `packed`, the variants of a record, the discriminants of a
schema, the bounds and index type of a conformant array parameter, and the
heading of a procedural one. The last three are not decoration. A schema
parameter is passed with its discriminants, and a conformant array with its
bounds, as arguments the call site adds; an importer reading a heading that did
not mention them would pass the array itself and the module would read a
pointer and a length that were never sent.

The file is produced by the compiler's `writePMIFiles()` pass, which serializes
the module's AST TypeNode tree back into Pascal source text and copies the
export list from the module's interface. Where a module is written as an
interface and a body, the interface is the part serialized: the body repeats
none of the types and writes its routines as the name alone. An initial state
(EP §6.6) written on a type is carried with it, and the importing unit starts
its variables of that type where the interface says they start. It is read by
`loadPMI()`, which
appends an empty program to make a compilation unit and then hands the
interface to the same `processModuleInterface()` that reads one written in the
source, so any valid Pascal type syntax that plang can parse can also appear in
a `.pmi` file.

---

## Current Limitations

- **No re-export.** An export list names what the module itself declares. A
  name it imported from elsewhere cannot be passed on. (A type is made opaque
  to importers by declaring it `restricted T`, per EP §6.4.2.5, and exporting
  it while keeping `T` in; that is implemented.)
- **No circular imports.** In both single-file and multi-file mode, modules must
  be defined or compiled before they are imported.
- **No dependency tracking.** The driver does not automatically recompile a
  module when its source changes. Run `plang -c Module.pas` manually to refresh
  `Module.o` and `Module.pmi` before recompiling dependents.
- **No goto from a module's procedure into its own `to begin do`/`to end do`.**
  A `goto` may reach a label placed directly in one of those statements from
  within that same statement — it never leaves the module's initializer or
  finalizer — but not from a procedure the module declares. Unlike a
  program's block or an enclosing procedure, which stay on the call stack for
  as long as anything nested in them could still run, a module's lifecycle
  statement returns once it finishes, while the module's own procedures stay
  callable for the rest of the program's life (including from another
  compilation unit that only imports the module). A non-local goto reached
  from one would not reliably find that statement's activation still there
  to land in, so it is rejected at compile time instead.
- **An expression the writer cannot put into words is left out.** Every
  type-denoter plang can parse can now be written back, so this reaches only
  constant values, and only those built from something other than literals,
  names, operators and calls. A declaration whose value or type cannot be
  written is dropped rather than guessed at, and the importing unit reports the
  name as undefined — a module that hits this must be compiled in the same file
  as its importers.
