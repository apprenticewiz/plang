(*
The Ctrl-Z, byte value 26, source-termination convention exercised right
next door, in ctrl-z-terminates-scanning-under-turbo.pas, is Turbo-only.
Neither ISO 7185 nor Extended Pascal has any such convention; that byte is
just another byte to them, scanned and, since it matches none of the
symbols scanSymbol() knows, rejected like any other.  Under -std=iso7185,
this file's default with no -std= flag at all, the scanner must keep going
past it and reach the identifier that follows, not stop there the way
-std=turbo does.

RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK: [[P2:[0-9]+:[0-9]+]]: Identifier "y"
CHECK: [[P3:[0-9]+:[0-9]+]]: Eof
*)

x
y
