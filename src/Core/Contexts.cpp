// Skyrim input contexts: count per game version, names and who owns their controls.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- vanilla controls

    int ContextCount()
    {
        // 1.5.97 has 17 input contexts, 1.6.1130+ has 18 (Marketplace)
        return REL::Module::IsAE() ? 18 : 17;
    }



    int LogicalContext(int c)
    {
        if (!REL::Module::IsAE() && c >= kCtxMarketplace) ++c;
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
