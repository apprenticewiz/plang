#pragma once

namespace plang {

/// Entry point for \c plang \c -pc1 (Pascal compiler front-end mode).
///
/// Invoked directly when the plang driver re-invokes itself with \c -pc1, or
/// called by embedders that link against \c libplang-frontend.
///
/// \p Argc and \p Argv are the full argument vector passed to \c main() —
/// \c Argv[0] is the binary name, \c Argv[1] is \c "-pc1", and option
/// parsing begins at \c Argv[2].
int frontendPC1Main(int Argc, char *Argv[]);

} // namespace plang
