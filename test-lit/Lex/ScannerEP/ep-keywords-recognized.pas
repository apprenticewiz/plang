(*
A representative set of EP-only reserved words, all recognized as
keywords under Extended Pascal -- one shared source rather than one file
per word, since each scans independently regardless of what surrounds it.

RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

and_then or_else otherwise module import export only qualified restricted bindable protected value pow

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: AndThen "and_then"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: OrElse "or_else"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Otherwise "otherwise"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Module "module"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Import "import"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Export "export"
CHECK-NEXT: [[P7:[0-9]+:[0-9]+]]: Only "only"
CHECK-NEXT: [[P8:[0-9]+:[0-9]+]]: Qualified "qualified"
CHECK-NEXT: [[P9:[0-9]+:[0-9]+]]: Restricted "restricted"
CHECK-NEXT: [[P10:[0-9]+:[0-9]+]]: Bindable "bindable"
CHECK-NEXT: [[P11:[0-9]+:[0-9]+]]: Protected "protected"
CHECK-NEXT: [[P12:[0-9]+:[0-9]+]]: Value "value"
CHECK-NEXT: [[P13:[0-9]+:[0-9]+]]: Pow "pow"
*)
