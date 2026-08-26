(*
The compiled-in messages are already English; opening a file to be told so
would be work for no result, and it is the common case: catalogSearchOrder
short-circuits to empty for en/en_US/no-tag-at-all, distinguishable from
"searched and found nothing" by describeLocale()'s own wording. A regional
English is a real catalog and is not short-circuited.

RUN: %plang -fdiagnostics-language=en_US --version | FileCheck --check-prefix=BUILTIN %s
RUN: %plang -fdiagnostics-language=en --version | FileCheck --check-prefix=BUILTIN2 %s
RUN: env -u LC_ALL -u LC_MESSAGES -u LANG %plang --version | FileCheck --check-prefix=BUILTIN %s
RUN: %plang -fdiagnostics-language=en_GB --version | FileCheck --check-prefix=REGIONAL %s
*)

(*
BUILTIN: Messages: en_US (built-in)
BUILTIN2: Messages: en (built-in)
REGIONAL: Messages: en_GB (
*)
