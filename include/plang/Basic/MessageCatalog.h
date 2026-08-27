#pragma once

/// MessageCatalog.h — diagnostic message text read at run time
///
/// A diagnostic's English is written in the four DiagnosticKinds .def
/// catalogs and compiled in, which is where it stays: this reads a
/// *translation* of it and falls back to the compiled-in English wherever it
/// has nothing better.  The fallback is total by design — missing, unreadable,
/// malformed, stale, partial and fuzzy all end in English rather than in an
/// error, because a compiler that cannot say what is wrong with a program
/// because a data file is absent is worse than one saying it in the wrong
/// language.
///
/// The format is GNU gettext's .po, read here rather than by libintl: the
/// format buys the translator Poedit, Weblate and msgmerge, and not linking
/// the library keeps macOS out of a dependency it does not ship in libSystem.
///
/// KEYS.  An entry is found by its `msgctxt`, which holds a namespaced
/// identifier — `diag/err_undefined_identifier` — and not, as gettext usually
/// does, the English text.  Two reasons.  plang already has stable
/// identifiers, so keying on English would untranslate the world every time a
/// word of it changed.  And the namespace is there from the first release
/// because two further kinds of text have to join later — the token
/// descriptions that arrive as %0 of "expected %0, got %1", and the
/// error/warning/note labels — and a key scheme that had to change to admit
/// them would invalidate every catalog written before it.
///
/// PLACEHOLDERS are the %0..%9 of the .def files, and a translation may
/// reorder them; see formatDiagMsg in Diagnostic.h.  A translation whose
/// placeholders are not the same *set* as the English is rejected, because
/// formatDiagMsg substitutes nothing for an index it has no argument for and
/// would otherwise produce a quietly truncated sentence.

#include "plang/Basic/Diagnostic.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace plang {

/// The compiled-in English for a diagnostic, expanded from the .def
/// catalogs.  Defined in lib/Basic/BuiltinCatalog.cpp.  Every lookup falls back here.
const char* builtinDiagFormat(DiagID ID);

/// The msgctxt namespaces.  A key is a namespace and a stable identifier:
/// `diag/err_undefined_identifier`, `token/Identifier`, `label/error`.
///
/// Three, because a message is not the only English in a diagnostic line.  The
/// token descriptions arrive as %0 of "expected %0, got %1", and the severity
/// label is printed in front of every message; leaving either out would give a
/// permanently half-translated line, which is the defect this feature exists
/// to remove rather than to relocate.
inline constexpr std::string_view PoDiagPrefix  = "diag/";
inline constexpr std::string_view PoTokenPrefix = "token/";
inline constexpr std::string_view PoLabelPrefix = "label/";

/// The label a severity is printed under, in English.  Defined in
/// lib/Basic/BuiltinCatalog.cpp beside the messages.
const char* builtinSeverityLabel(DiagSeverity Sev);

/// What a load did, for a caller that wants to say so.  A load can succeed
/// while ignoring entries: that is the normal state of a translation in
/// progress, not an error.
struct CatalogReport {
    unsigned Loaded{0};   ///< entries accepted and now in use
    unsigned Fuzzy{0};    ///< skipped for being marked `#, fuzzy`
    unsigned Untranslated{0}; ///< skipped for an empty msgstr
    unsigned Unknown{0};  ///< msgctxt naming something this plang has no message for,
                          ///< or no msgctxt at all on an entry that is not the header
    unsigned Malformed{0};///< entries dropped; the rest of the file was still read
    /// Set only when the file was rejected outright, and then nothing was
    /// loaded and the previous contents are untouched.
    std::string FatalReason;

    [[nodiscard]] bool ok() const { return FatalReason.empty(); }
};

/// Where a catalog was found, and what came of it, so that --version can say
/// so.  Falling back to English is silent by design, which is what makes a
/// catalog installed somewhere plang does not look invisible — this is how a
/// build says out loud which one it is using.
struct LocaleResolution {
    std::string Requested;  ///< the tag asked for, normalized; empty if none
    std::string Language;   ///< the most specific catalog used; empty for built-in
    std::string Path;       ///< the most specific file loaded; empty when none
    /// Every file that went into the catalog, least specific first, so that
    /// --version can say a regional catalog was laid over its base.
    std::vector<std::string> Loaded;
    CatalogReport Report;

    /// True when messages are coming from the compiled-in English, either
    /// because that is what was asked for or because nothing else was found.
    [[nodiscard]] bool builtin() const { return Path.empty(); }
};

/// Reads LC_ALL, then LC_MESSAGES, then LANG — the POSIX order — and returns
/// the message language, or empty for none.
///
/// plang never calls setlocale().  Its scanner decides what a letter and a
/// digit are with <cctype>, so a real locale would change which identifiers it
/// accepts and how it decodes a non-decimal literal; that is a question about
/// the language plang compiles and must not depend on the environment.  Only
/// the message text is localized, so only the message text reads these.
std::string localeFromEnvironment();

/// Reduces a POSIX locale spelling to the tag a catalog is named by:
/// "fr_CA.UTF-8@euro" -> "fr_CA", "C" and "POSIX" -> "".
std::string normaliseLocale(std::string_view Spec);

/// The catalogs to try for \p Tag, most specific first: "fr_CA" gives
/// {"fr_CA", "fr"}.  Empty when \p Tag names English or nothing, since the
/// compiled-in messages are already English and reading a file to be told so
/// would be work for no result.
std::vector<std::string> catalogSearchOrder(std::string_view Tag);

/// Directories to look in, most specific first: PLANG_LOCALE_DIR if it is set,
/// then <the directory holding this binary>/../share/plang/locale, then the
/// build-tree path compiled in as PLANG_CATALOG_DIR.
///
/// The two-step is the same shape the driver already uses to find its runtime
/// library, and for the same reason: in the build tree the binary sits at
/// tools/driver/plang, so ../share is not where the catalogs are, and only the
/// compiled-in path finds them.  The test binaries make it sharper still —
/// they are the main executable, so the relative path is wrong for all of them.
std::vector<std::string> catalogSearchPaths(const std::string& ExeDir);

/// Resolves \p Tag (or the environment when it is empty) and loads whatever it
/// finds into the active catalog.  Never fails: everything that goes wrong
/// ends with the compiled-in English and is recorded in the result.
LocaleResolution selectLocale(std::string_view Tag, const std::string& ExeDir,
                              bool HonourFuzzy = false);

/// What selectLocale last resolved, or a default-constructed resolution if it
/// has not been called -- which means the compiled-in English, the state an
/// embedder that never asked for a locale is in.
const LocaleResolution& currentLocale();

/// One line for --version saying where messages are actually coming from.
///
/// This reports the resolution that happened rather than working it out again
/// from the environment, because the two can differ: -fdiagnostics-language=
/// overrides the environment, and a --version that recomputed would name the
/// language the machine is set to while the compiler used the one asked for.
/// It is also the only positive evidence that a catalog was found at all --
/// everything else about a missing one is, deliberately, indistinguishable
/// from never having asked for one.
std::string describeLocale();

class MessageCatalog {
public:
    /// The catalog every diagnostic lookup consults.  Empty until something
    /// loads one, and empty means every lookup falls back to English — which
    /// is exactly what a build nobody has told about a locale should do.
    static MessageCatalog& active();

    /// Reads a .po file.  Refuses anything that is not a regular file, and
    /// anything larger than MaxCatalogBytes, so that pointing plang at a FIFO
    /// or at /dev/zero cannot hang it or exhaust its memory.
    /// \p Merge keeps what is already loaded and lets this file override it,
    /// which is how a regional catalog is laid over the language it refines:
    /// es_MX.po names only what differs from es.po and must not discard the
    /// rest of it.
    CatalogReport loadFile(const std::string& Path, bool Merge = false);

    /// Reads .po text already in memory.  A malformed *entry* is dropped and
    /// counted; the rest of the file is still read, so one bad line costs one
    /// message rather than the whole translation.  A malformed *file* — one
    /// whose charset is not UTF-8, or which needs a newer reader — is refused
    /// whole, and the catalog keeps whatever it had.
    CatalogReport loadText(std::string_view Text, bool Merge = false);

    /// The translation for a diagnostic, or null when there is none.  The
    /// pointer belongs to the catalog and is valid until the next load or
    /// clear; nothing in plang retains one, because DiagnosticsEngine::report
    /// formats eagerly and keeps only the finished string.
    [[nodiscard]] const char* lookupDiag(DiagID ID) const;

    /// The translation for a token description -- "identifier", "end of file"
    /// -- or null.  Only the tokens with no fixed spelling have one; a token
    /// that is spelled ';' or 'begin' is Pascal syntax and is never translated.
    [[nodiscard]] const char* lookupToken(std::string_view KindName) const;

    /// The translation for a severity label, or null.
    [[nodiscard]] const char* lookupLabel(DiagSeverity Sev) const;

    /// Honor entries a translator marked `#, fuzzy`.  Off by default: gettext
    /// excludes fuzzy entries from a compiled catalog, msgmerge produces them
    /// by string similarity, and an unreviewed guess at what a compiler error
    /// means is worse than English.  A reviewer has to see them to review
    /// them, which is what this is for.  Set it before loading — fuzzy entries
    /// are dropped as they are read, not at lookup.
    void setHonorFuzzy(bool Honor) { HonourFuzzy = Honor; }
    [[nodiscard]] bool honoursFuzzy() const { return HonourFuzzy; }

    [[nodiscard]] std::size_t size()  const { return Entries.size(); }
    [[nodiscard]] bool        empty() const { return Entries.empty(); }

    /// Invalidates every pointer previously returned by a lookup.
    void clear() { Entries.clear(); }

    /// The largest catalog that will be read.  The whole English catalog is
    /// under 20 kB, so this is three orders of magnitude of headroom and still
    /// small enough that a runaway file is refused rather than read.
    static constexpr std::size_t MaxCatalogBytes = 8u * 1024u * 1024u;

private:
    /// Keyed by the whole msgctxt, namespace included, so that the three kinds
    /// cannot collide and a key names exactly one thing.  Node-based, so the
    /// pointers a lookup returns survive later insertions.
    std::unordered_map<std::string, std::string> Entries;
    bool HonourFuzzy{false};
};

} // namespace plang
