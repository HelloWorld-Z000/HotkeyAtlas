// While a bind is captured, the keyboard and the middle / side mouse buttons and wheel reach
// no one: the input hook hands their events to the capture instead and hides them from the game
// and from every mod that reads the game's input (dMenu, Wheeler...), so the key picked for a
// bind doesn't also open whatever is on it. (A low-level Windows hook would also stop mods that
// read Windows directly, but Skyrim's DirectInput takes the keyboard before such a hook.)

#include "Internal.h"

namespace HA
{
    namespace
    {
        std::mutex                g_lock;
        std::vector<BlockedInput> g_events;  // since the last TakeBlockedInput()
        std::set<std::uint32_t>   g_held;    // device << 16 | id of what was pressed while blocking
        std::atomic<bool>         g_on{ false };
        std::atomic<bool>         g_anyHeld{ false };

        constexpr std::uint32_t HeldId(Device d, std::uint32_t id) { return static_cast<std::uint32_t>(d) << 16 | id; }
    }

    void StartInputBlock()
    {
        std::lock_guard l(g_lock);
        g_events.clear();
        g_on = true;
    }

    void StopInputBlock()
    {
        std::lock_guard l(g_lock);
        g_on = false;
        g_events.clear();
    }

    bool InputBlockBusy() { return g_on || g_anyHeld; }

    // Game thread, from the input hook. Off (capture over), the keys it took stay hidden until
    // they are let go, so no mod sees a lone release.
    bool BlockInputEvent(Device d, std::uint32_t id, bool down, bool up, bool canHold)
    {
        std::lock_guard l(g_lock);
        const auto      key = HeldId(d, id);
        if (!g_on) {
            if (!g_held.contains(key)) return false;
            if (up) g_held.erase(key);
            g_anyHeld = !g_held.empty();
            return true;
        }
        if (down) {
            if (canHold) g_held.insert(key);
            g_events.push_back({ d, id, true });
        } else if (up) {
            g_held.erase(key);
            g_events.push_back({ d, id, false });
        }
        g_anyHeld = !g_held.empty();
        return true;
    }

    std::vector<BlockedInput> TakeBlockedInput()
    {
        std::lock_guard l(g_lock);
        return std::exchange(g_events, {});
    }

    bool BlockedHeld(Device d, std::uint32_t id)
    {
        std::lock_guard l(g_lock);
        return g_held.contains(HeldId(d, id));
    }

    bool AnyBlockedHeld() { return g_anyHeld; }
}
