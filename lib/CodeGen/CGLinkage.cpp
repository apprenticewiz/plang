#include "CGLinkage.h"

#include "plang/Basic/StringUtil.h"

using namespace plang;

std::string CGLinkage::moduleScope(const std::string& moduleName) {
    return moduleName.empty() ? "" : toLower(moduleName) + PlangScopeSep;
}

std::string CGLinkage::stripQualifier(const std::string& name) {
    const auto dot = name.rfind('.');
    return (dot == std::string::npos) ? name : name.substr(dot + 1);
}

const ImportedName* CGLinkage::importedName(const std::string& name) const {
    if (!ImportOwners) return nullptr;
    auto unit = ImportOwners->find(CurrentUnit);
    if (unit == ImportOwners->end()) return nullptr;
    auto it = unit->second.find(toLower(name));
    return it == unit->second.end() ? nullptr : &it->second;
}

std::string CGLinkage::importOwner(const std::string& name) const {
    if (const auto* imp = importedName(name)) return imp->Module;
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? std::string()
                                    : toLower(name.substr(0, dot));
}

std::string CGLinkage::importLinkName(const std::string& name) const {
    if (const auto* imp = importedName(name))
        if (!imp->LinkName.empty()) return imp->LinkName;
    return stripQualifier(name);
}

bool CGLinkage::isImportedCallable(const std::string& name) const {
    const auto* imp = importedName(name);
    return imp && imp->IsCallable;
}

std::string CGLinkage::findMangledProc(const std::string& qualifiedName) const {
    const std::string      name = stripQualifier(qualifiedName);
    // EP §6.11.2: `M.f` names f as M's export, on purpose, to reach past
    // an importer's own homonym -- that is the one thing `qualified`
    // buys over a plain import.  The enclosing-scope walk below answers
    // a BARE name by asking what is visible here, which is the wrong
    // question for one that was written qualified: `writeln(f); writeln(
    // M.f)` inside a procedure that declares its own `f` called its own
    // `f` for both, because the walk found it before the qualifier was
    // ever consulted.  A qualified name skips straight to the import.
    if (qualifiedName.find('.') != std::string::npos)
        return PlangProcPrefix + moduleScope(importOwner(qualifiedName))
             + importLinkName(qualifiedName);
    const std::size_t      root = std::string_view(PlangProcPrefix).size();
    const std::string_view sep(PlangScopeSep);
    std::string prefix = NamePrefix;
    while (true) {
        std::string candidate = prefix + name;
        if (Mod.getFunction(candidate)) return candidate;
        // Strip the innermost enclosing scope: "pas_outer$inner$" →
        // "pas_outer$".  prefix ends with the separator, so the search
        // starts before it, at the last character of the scope name.
        if (prefix.size() <= root) break; // the bare prefix is the last try
        auto pos = prefix.rfind(sep, prefix.size() - sep.size() - 1);
        if (pos == std::string::npos || pos + sep.size() < root) break;
        prefix = prefix.substr(0, pos + sep.size());
    }
    // Nothing of that name is in scope here, so it is imported.  The walk
    // above runs first so a procedure this unit declares itself still wins
    // over one of the same name that it imports.
    return PlangProcPrefix + moduleScope(importOwner(qualifiedName))
         + importLinkName(qualifiedName);
}

std::string CGLinkage::mangledMethod(const std::string& objectTypeName,
                                      const std::string& methodName,
                                      const std::string& declaringModule) const {
    const std::string& Unit = declaringModule.empty() ? CurrentUnit : declaringModule;
    return PlangProcPrefix + moduleScope(Unit)
         + toLower(objectTypeName) + PlangScopeSep + toLower(methodName);
}

std::string CGLinkage::mangledVmt(const std::string& objectTypeName,
                                  const std::string& declaringModule) const {
    const std::string& Unit = declaringModule.empty() ? CurrentUnit : declaringModule;
    return "pas_vmt$" + moduleScope(Unit) + toLower(objectTypeName);
}

std::string CGLinkage::mangledGlobal(const std::string& qualifiedName) const {
    const std::string name = stripQualifier(qualifiedName);
    // Same reasoning as findMangledProc just above: a qualified `M.v`
    // must reach M's global even when this translation unit happens to
    // define its own `v`, so the qualified case skips the bare-name
    // check entirely rather than letting a same-spelling local answer
    // for an explicitly-qualified reference.
    if (qualifiedName.find('.') == std::string::npos
            && Mod.getGlobalVariable(GlobalPrefix + name))
        return GlobalPrefix + name;
    return PlangGlobalPrefix + moduleScope(importOwner(qualifiedName))
         + importLinkName(qualifiedName);
}
