// Goal: Manage the firmware's Wi-Fi connection and setup lifecycle.
// Responsibilities: Validate credentials and track connection and setup state
// independently of the platform-specific radio driver.
// How it works:
// 1. Startup loads saved credentials through the driver.
// 2. Setup can accept a candidate and initiate a connection.
// 3. Periodic updates report connection state and failures.
#pragma once

#include <stdint.h>
#include <string.h>

namespace lab {

inline constexpr const char* kSetupSsid = "NETBOT-SETUP";

struct Credentials {
    char ssid[33]{};
    char password[64]{};
    bool valid() const {
        if (!memchr(ssid, 0, sizeof(ssid)) || !memchr(password, 0, sizeof(password))) return false;
        const size_t length = strlen(password);
        for (size_t i = 0; i < length; ++i)
            if (password[i] < 32 || password[i] > 126) return false;
        return ssid[0] && (!length || length >= 8);
    }
};
enum class LoadResult { Empty, Saved, Error };
enum class NetworkState { Unconfigured, Connecting, Connected, Offline, Error };
inline const char* networkName(NetworkState state) {
    switch (state) {
        case NetworkState::Unconfigured: return "Unconfigured";
        case NetworkState::Connecting: return "Connecting";
        case NetworkState::Connected: return "Connected";
        case NetworkState::Offline: return "Offline";
        default: return "Error";
    }
}
// Stable lowercase codes for the setup page and the local C# panel.
inline const char* networkCode(NetworkState state) {
    switch (state) {
        case NetworkState::Unconfigured: return "unconfigured";
        case NetworkState::Connecting: return "connecting";
        case NetworkState::Connected: return "connected";
        case NetworkState::Offline: return "offline";
        default: return "error";
    }
}
class NetworkDriver {
public:
    virtual ~NetworkDriver() = default;
    virtual LoadResult load(Credentials& value) = 0;
    virtual bool save(const Credentials& value) = 0;
    virtual bool join(const Credentials& value) = 0;
    virtual bool connected() const = 0;
    virtual bool advance() = 0;
    virtual bool disconnect() = 0;
    virtual bool startAp() = 0;
    virtual bool endAp() = 0;
};

class Network {
public:
    explicit Network(NetworkDriver& driver) : driver(driver) {}
    NetworkState state = NetworkState::Unconfigured;
    const char* error = nullptr;
    bool ap = false;
    bool lostConnection = false;

    void begin(uint32_t now) {
        const auto loaded = driver.load(saved);
        if (loaded == LoadResult::Error || (loaded == LoadResult::Saved && !saved.valid()))
            fail("config_read_failed");
        else if (loaded == LoadResult::Saved) connect(saved, now);
        else saved = {};
    }
    bool setup(uint32_t now) {
        if (ap) return true;
        if (!driver.startAp()) { error = "setup_unavailable"; return false; }
        ap = true;
        apBegan = now;
        error = nullptr;
        return true;
    }
    bool cancelSetup() {
        if (!ap) return true;
        if (!driver.endAp()) { error = "setup_close_failed"; return false; }
        ap = false;
        if (pendingSave) {
            pendingSave = false;
            candidate = {};
            if (!driver.disconnect()) { fail("disconnect_failed"); return false; }
            state = saved.valid() ? NetworkState::Offline : NetworkState::Unconfigured;
        }
        return true;
    }
    bool configure(const Credentials& value, uint32_t now) {
        if (!ap || !value.valid() || pendingSave) { error = "invalid_config"; return false; }
        candidate = value;
        pendingSave = true;
        return connect(candidate, now);
    }
    void tick(uint32_t now) {
        lostConnection = false;
        if (!driver.advance()) {
            pendingSave = false;
            candidate = {};
            fail("connection_start_failed");
            return;
        }
        if (ap && now - apBegan >= 180000) cancelSetup();
        if (state == NetworkState::Connecting) {
            if (driver.connected()) {
                if (pendingSave) {
                    const bool changed = strcmp(saved.ssid, candidate.ssid) || strcmp(saved.password, candidate.password);
                    if (changed && !driver.save(candidate)) {
                        pendingSave = false;
                        candidate = {};
                        fail("config_save_failed");
                        return;
                    }
                    saved = candidate;
                    candidate = {};
                    pendingSave = false;
                }
                state = NetworkState::Connected;
                error = nullptr;
                cancelSetup();
            } else if (now - attemptedAt >= 15000) {
                pendingSave = false;
                candidate = {};
                if (!driver.disconnect()) { fail("disconnect_failed"); return; }
                state = NetworkState::Offline;
                error = "connection_failed";
                attemptedAt = now;
            }
        } else if (state == NetworkState::Connected && !driver.connected()) {
            lostConnection = true;
            state = NetworkState::Offline;
            error = "connection_lost";
            attemptedAt = now;
        } else if (state == NetworkState::Offline && saved.valid() && now - attemptedAt >= 10000) {
            connect(saved, now);
        }
    }

private:
    NetworkDriver& driver;
    Credentials saved, candidate;
    uint32_t attemptedAt = 0, apBegan = 0;
    bool pendingSave = false;
    bool connect(const Credentials& value, uint32_t now) {
        attemptedAt = now;
        if (!driver.join(value)) {
            pendingSave = false;
            candidate = {};
            fail("connection_start_failed");
            return false;
        }
        state = NetworkState::Connecting;
        error = nullptr;
        return true;
    }
    void fail(const char* code) { state = NetworkState::Error; error = code; }
};

}  // namespace lab
