#pragma once

// Archipelago client built into the DLL. It talks to the server (ap_net) and drives the game with
// the same text commands as the debug bridge, injected as the "internal client". While connected,
// every 2 s: where + poi, then a resync. Reconnects every 5 s.
//
// Next to the DLL:
//   BioShockAP.ini         last login, auto-connect and debug flags
//   BioShockAP_state.json  per seed: checks made in game (resent on each connection, so offline
//                          purchases aren't lost) and items already announced
// The count of items already given lives in the game's save.
//
// Tested without the game by tests/loop_test.cpp. Game thread only.

#include <functional>
#include <string>

namespace bsap::apclient {

struct Address {
    std::string name;  // slot name (UTF-8)
    std::string host;
    int port = 38281;
    int scheme = 0;  // 0 auto, 1 wss, 2 ws
    std::string HostPort() const { return host + ":" + std::to_string(port); }
};

// "name@server:port" (port 38281 by default; optional ws:// or wss:// before the server).
bool ParseAddress(const std::string& text, Address* out);

enum class State { Idle, Connecting, Connected, Reconnecting };

enum class Failure {
    None,
    Unreachable,
    InvalidSlot,
    InvalidGame,
    InvalidPassword,
    IncompatibleVersion,
    Refused,  // FailureDetail()
};

void Connect(const Address& address, const std::string& password);
void Disconnect();

State GetState();
Failure LastFailure();
std::string FailureDetail();
const Address& CurrentAddress();

// Last successful connection (empty if none).
void Saved(std::string* address, std::string* password);
// Connects with the last successful connection; false if there is none.
bool ConnectSaved();

// autoconnect= in BioShockAP.ini (on unless 0): connect with the saved login when the game starts.
bool AutoConnectEnabled();
void SetAutoConnect(bool enabled);

// debug=1 in BioShockAP.ini: debug overlay, cheat keys and the TCP bridge.
bool DebugEnabled();

void Tick();

// Shown by ap_notify.
struct Notice {
    enum class Kind {
        ItemReceived,  // item = item, player = sender (another player)
        ItemSent,      // item = item, player = receiver
        ItemFound,     // item = our own item found in a level object (Voxophone, Infusion)
        DeathLink,     // text = cause, player = player who died
        Goal,
        ConnectionLost,
        Reconnected,
    };
    Kind kind;
    std::string item, player, text;  // UTF-8
};
void SetNotifier(std::function<void(const Notice&)> notifier);

enum class LocationStatus { NotALocation, ToDo, Done };
LocationStatus VoxophoneStatus(const std::string& id);
LocationStatus InfusionStatus(const std::string& id);
LocationStatus GearStatus(const std::string& id);
// AP rows not bought yet on this machine (game path); -1 if it is not an AP machine.
int MachineRemaining(const std::string& path);
std::size_t CheckedCount();
std::size_t MissingCount();

}  // namespace bsap::apclient
