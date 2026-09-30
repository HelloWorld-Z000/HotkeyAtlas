// Skyrim input contexts: count per game version, names and who owns their controls.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- vanilla controls

    // Marketplace came with 1.6.1130, not with AE: 1.5.97, 1.6.317-1.6.659 (incl. GOG 1.6.659)
    // and VR have 17 contexts, and ControlMap's runtime data follows right after them.
    static bool HasMarketplace()
    {
        static const bool has = REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_6_1130;
        return has;
    }

    int ContextCount()
    {
        return HasMarketplace() ? 18 : 17;
    }

    int LogicalContext(int c)
    {
        if (!HasMarketplace() && c >= kCtxMarketplace) ++c;
        return c >= 0 && c < kCtxCount ? c : kCtxAny;
    }

    std::string ContextName(int c)
    {
        const auto lc = LogicalContext(c);
        return lc == kCtxAny ? TLF("Context {0}", { std::to_string(c) }) : TL(kCtxInfo[lc].name);
    }

    std::string ContextHint(int c)
    {
        const auto lc = LogicalContext(c);
        return lc == kCtxAny ? std::string() : TL(kCtxInfo[lc].where);
    }

    // Owner of a vanilla control. Creation Club and the developer contexts get their own
    // owners so they can be hidden in the blacklist.
    std::string VanillaOwner(int c)
    {
        switch (LogicalContext(c)) {
        case kCtxMarketplace:
            return "Skyrim - Creation Club";
        case kCtxDebugText:
        case kCtxDebugOverlay:
        case kCtxMapDebug:
        case kCtxTFC:
            return "Skyrim - Debug";
        default:
            return "Skyrim";
        }
    }
}
