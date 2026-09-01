(*
describeLocale() always echoes the *normalized* tag, never the raw
-fdiagnostics-language= argument, in all three of its branches -- so
--version is a precise proxy for normaliseLocale()'s own return value: a
bug that failed to strip a codeset/modifier or a POSIX "C"/"POSIX" spelling
would show the untouched raw tag instead of the normalized one below.

RUN: %plang -fdiagnostics-language=fr_CA.UTF-8@euro --version | FileCheck --check-prefix=FRCA %s
RUN: %plang -fdiagnostics-language=de_DE.utf8 --version | FileCheck --check-prefix=DEDE %s
RUN: %plang -fdiagnostics-language=es_MX --version | FileCheck --check-prefix=ESMX %s
RUN: %plang -fdiagnostics-language=C --version | FileCheck --check-prefix=NEITHER %s
RUN: %plang -fdiagnostics-language=POSIX --version | FileCheck --check-prefix=NEITHER %s
*)

(*
FRCA: Messages: fr_CA (
DEDE: Messages: de_DE (no catalog found; using built-in en_US)
ESMX: Messages: es_MX (
NEITHER: Messages: en_US (built-in)
*)
