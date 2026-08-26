(*
The same EP-only reserved words as ep-keywords-recognized.pas must be
plain identifiers under the default (ISO 7185) dialect.  Two of them
(and_then, or_else) contain an underscore, itself an Extended Pascal
extension, so scanning them under the default dialect also raises an
error -- the scanner recovers and still hands back the right identifier
token for every word, which is what this test actually cares about, so
the RUN line tolerates the nonzero exit rather than asserting success.

RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

and_then or_else otherwise module import export only qualified restricted bindable protected value pow

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "and_then"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "or_else"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier "otherwise"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "module"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Identifier "import"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Identifier "export"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: Identifier "only"
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: Identifier "qualified"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: Identifier "restricted"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Identifier "bindable"
CHECK-NEXT: [[P11:[0-9]+:[0-9]+]]: Identifier "protected"
CHECK-NEXT: [[P12:[0-9]+:[0-9]+]]: Identifier "value"
CHECK-NEXT: [[P13:[0-9]+:[0-9]+]]: Identifier "pow"
*)
