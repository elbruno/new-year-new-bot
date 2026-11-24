// Goal: Connect the Wi-Fi firmware to its local HTTP interface.
// Responsibilities: Manage network and server lifecycle, register routes,
// validate requests, and dispatch them to the shared device runtime.
// How it works:
// 1. Begin initializes networking and registers profile-specific HTTP routes.
// 2. Request handlers return status or validate and forward requested actions.
// 3. Periodic ticks advance network state and coordinate connection loss.
#pragma once

#include <atomic>
#include <stdarg.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <nvs.h>
#include "SetupPassword.h"
#include "LabApi.h"
#include "LabDevice.h"
#include "LabNetwork.h"
#include "LabPages.h"
#include "LabPresence.h"
#include "LabOperationalEvents.h"
#include "LabRemoteLog.h"
#include "AvatarComparison.h"
#include "CameraProducer.h"
#if !defined(NETBOT_TEST_BUILD)
#include "CameraHardware.h"
#endif

#if !defined(NETBOT_TEST_BUILD) && __has_include("wifi_setup_private.h")
#include "wifi_setup_private.h"
#ifndef NETBOT_DEV_SETUP_PASSWORD
#error "wifi_setup_private.h must define NETBOT_DEV_SETUP_PASSWORD."
#endif
#define LAB_HAS_DEV_SETUP_PASSWORD 1
#else
#define LAB_HAS_DEV_SETUP_PASSWORD 0
#endif

namespace lab {

#if LAB_HAS_DEV_SETUP_PASSWORD
static_assert(setup_password::isValidDevelopmentPassword(NETBOT_DEV_SETUP_PASSWORD),
    "The local development AP password must be exactly 8 printable ASCII characters.");
inline constexpr const char* kDevelopmentSetupPassword = NETBOT_DEV_SETUP_PASSWORD;
#else
inline constexpr const char* kDevelopmentSetupPassword = nullptr;
#endif

class Radio : public NetworkDriver {
    Credentials next;
    bool joining = false;
    char setupKey[setup_password::kBufferSize]{};
public:
    bool ready = false;
    bool begin() {
        esp_log_level_set("wifi", ESP_LOG_NONE);
        WiFi.persistent(false);
        WiFi.setHostname("netbot-lab");
        if (!WiFi.mode(WIFI_STA) || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) return false;
        WiFi.setAutoReconnect(false);
        ready = true;
        return true;
    }
    LoadResult load(Credentials& value) override {
        nvs_handle_t handle;
        const esp_err_t opened = nvs_open("netbot_lab", NVS_READONLY, &handle);
        if (opened == ESP_ERR_NVS_NOT_FOUND) return LoadResult::Empty;
        if (opened != ESP_OK) return LoadResult::Error;
        size_t length = sizeof(value);
        const esp_err_t result = nvs_get_blob(handle, "wifi_v1", &value, &length);
        nvs_close(handle);
        if (result == ESP_ERR_NVS_NOT_FOUND) return LoadResult::Empty;
        return result == ESP_OK && length == sizeof(value) && value.valid() ? LoadResult::Saved : LoadResult::Error;
    }
    bool save(const Credentials& value) override {
        nvs_handle_t handle;
        if (nvs_open("netbot_lab", NVS_READWRITE, &handle) != ESP_OK) return false;
        const bool written = nvs_set_blob(handle, "wifi_v1", &value, sizeof(value)) == ESP_OK &&
            nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
        return written;
    }
    bool join(const Credentials& value) override {
        if (!ready || !disconnect()) return false;
        next = value;
        joining = true;
        return true;
    }
    bool advance() override {
        if (!joining || WiFi.status() == WL_CONNECTED) return true;
        joining = false;
        wifi_config_t config{};
        memcpy(config.sta.ssid, next.ssid, strlen(next.ssid));
        memcpy(config.sta.password, next.password, strlen(next.password));
        config.sta.threshold.authmode = next.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        config.sta.pmf_cfg.capable = true;
        next = {};
        return esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK;
    }
    bool connected() const override { return !joining && WiFi.status() == WL_CONNECTED; }
    bool disconnect() override {
        joining = false;
        next = {};
        const esp_err_t result = esp_wifi_disconnect();
        return result == ESP_OK || result == ESP_ERR_WIFI_NOT_CONNECT;
    }
    bool startAp() override {
        if (!ready) return false;
        if (!setup_password::create(setupKey, sizeof(setupKey), kDevelopmentSetupPassword,
                                    [] { return esp_random(); })) {
            clearSetupKey();
            return false;
        }
        if (WiFi.softAP(kSetupSsid, setupKey, 1, 0, 1)) return true;
        clearSetupKey();
        return false;
    }
    // softAPdisconnect(false) clears security settings but leaves AP enabled.
    bool endAp() override {
        if (!WiFi.enableAP(false)) return false;
        clearSetupKey();
        return true;
    }
    const char* setupPassword() const { return setupKey[0] ? setupKey : nullptr; }

private:
    void clearSetupKey() {
        volatile char* value = setupKey;
        for (size_t i = 0; i < sizeof(setupKey); ++i) value[i] = '\0';
    }
};

#undef LAB_HAS_DEV_SETUP_PASSWORD
#ifdef NETBOT_DEV_SETUP_PASSWORD
#undef NETBOT_DEV_SETUP_PASSWORD
#endif

inline uint32_t newBootId() {
    const uint32_t value = esp_random();
    return value ? value : 1;
}

// Connect the network and HTTP server to the shared firmware Device runtime.
class Platform {
    Radio radio;
public:
    explicit Platform(Device& device) : network(radio), device(device) {}
    AvatarComparison avatarComparison;
    std::mutex avatarComparisonMutex;
    Network network;
    const char* serverError = nullptr;
    const char* cameraStartupError = nullptr;
    bool remoteUi = false;  // Wi-Fi profiles enable this only under the trusted isolated-LAN policy.
    RemoteLog* remoteLog = nullptr;
    CameraProducer* camera = nullptr;

    bool recordOperationalEvent(uint32_t atMs, OperationalEventKind event, OperationalStatus status,
                                OperationalAction action, OperationalError error,
                                uint32_t correlationId = 0, uint32_t relatedId = 0, uint32_t revision = 0,
                                OperationalMetadata metadata = {}) {
        return operationalEvents.append(atMs, event, status, action, error,
            correlationId, relatedId, revision, metadata);
    }

    bool companionPresent(uint32_t now) const { return companionPresence.present(now); }
    void updateCompanionPresence(bool connected, uint32_t now) { companionPresence.update(connected, now); }

    bool motionTestNetworkReady() {
        std::lock_guard<std::mutex> lock(networkMutex);
        return !network.ap && network.state == NetworkState::Connected &&
            radio.connected() && static_cast<uint32_t>(WiFi.localIP()) != 0;
    }

    void initializeDisplayBrightness() {
        std::lock_guard<std::mutex> lock(displayBrightnessMutex);
        if (displayBrightnessInitialized) return;
        M5.Display.setBrightness(kDisplayBrightnessDefault);
        displayBrightness = M5.Display.getBrightness();
        requestedDisplayBrightness = displayBrightness;
        displayBrightnessError = displayBrightness == kDisplayBrightnessDefault ?
            nullptr : "brightness_readback_mismatch";
        displayBrightnessInitialized = true;
    }

    bool displayBrightnessStatus(DisplayBrightnessStatus& state, bool httpRequest = false,
                                 bool* contended = nullptr) {
        if (contended) *contended = false;
        auto lock = httpRequest ? httpLock(displayBrightnessMutex) :
            std::unique_lock<std::mutex>(displayBrightnessMutex);
        if (!lock.owns_lock()) {
            if (contended) *contended = true;
            return false;
        }
        if (!displayBrightnessInitialized) return false;
        state = {displayBrightness, requestedDisplayBrightness, displayBrightnessPending,
            displayBrightnessError};
        return true;
    }

    DisplayBrightnessQueueResult requestDisplayBrightness(uint8_t requested,
            InteractionOrigin origin = InteractionOrigin::Unknown, bool httpRequest = false,
            DisplayBrightnessStatus* acceptedState = nullptr) {
        if (requested < kDisplayBrightnessMinimum || requested > kDisplayBrightnessMaximum)
            return DisplayBrightnessQueueResult::OutOfRange;
        auto lock = httpRequest ? httpLock(displayBrightnessMutex) :
            std::unique_lock<std::mutex>(displayBrightnessMutex);
        if (!lock.owns_lock()) return DisplayBrightnessQueueResult::Busy;
        if (!displayBrightnessInitialized) return DisplayBrightnessQueueResult::Unavailable;
        if (displayBrightnessPending) return DisplayBrightnessQueueResult::Busy;
        requestedDisplayBrightness = requested;
        displayBrightnessOrigin = origin;
        displayBrightnessPending = true;
        displayBrightnessError = nullptr;
        recordOperationalEvent(millis(), OperationalEventKind::Brightness, OperationalStatus::Requested,
            OperationalAction::None, OperationalError::None, 0, 0, 0,
            {origin, OperationalMetadata::Requested, requested, 0, 0});
        if (acceptedState)
            *acceptedState = {displayBrightness, requestedDisplayBrightness, displayBrightnessPending,
                displayBrightnessError};
        return DisplayBrightnessQueueResult::Accepted;
    }

    // Initialize networking and register the routes supported by this build profile.
    void begin(uint32_t now) {
        initializeDisplayBrightness();
        operationalEvents.reset(device.snapshot().bootId);
        recordOperationalEvent(now, OperationalEventKind::Boot, OperationalStatus::Ready,
            OperationalAction::None, OperationalError::None);
        {
            std::lock_guard<std::mutex> lock(device.mutex);
            device.notification.reset(device.runtime.snapshot().bootId,
                [](void* context, const OperationalEventEntry& event) {
                    static_cast<Platform*>(context)->recordOperationalEvent(event.atMs, event.event,
                        event.status, event.action, event.error, event.correlationId, event.relatedId,
                        event.revision, event.metadata);
                }, this);
        }
        logsEnabled = remoteLog != nullptr;
        if (!radio.begin()) {
            network.state = NetworkState::Error;
            network.error = "radio_init_failed";
        } else {
            network.begin(now);
            network.tick(now);
        }
#if !defined(NETBOT_TEST_BUILD)
        static CameraHardware cameraHardware;
        static CameraProducer cameraProducer(cameraHardware, launchCameraTask, finishCameraTask);
        camera = &cameraProducer;
#endif
        if (camera && !camera->initialize(device.snapshot().bootId))
            cameraStartupError = "camera_boot_init_failed";
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.stack_size = 8192;
        config.max_open_sockets = 4;
        config.lru_purge_enable = true;
        config.recv_wait_timeout = 1;
        config.send_wait_timeout = 1;
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
        config.max_uri_handlers = 45;
#else
        config.max_uri_handlers = 32;
#endif
        if (httpd_start(&server, &config) != ESP_OK) { serverError = "http_start_failed"; return; }
        // Status and version are reads; actions and Stop are requests to the runtime.
        const struct { const char* path; httpd_method_t method; } routes[] = {
            {"/", HTTP_GET}, {"/api/status", HTTP_GET}, {"/api/network", HTTP_GET},
            {"/api/display/brightness", HTTP_GET}, {"/api/display/brightness", HTTP_POST},
            {"/api/actions", HTTP_POST},
            {"/api/stop", HTTP_POST}, {"/setup", HTTP_GET}, {"/api/config", HTTP_POST}, {"/api/version", HTTP_GET},
            {"/api/motion", HTTP_GET}, {"/api/motion", HTTP_POST},
            {"/api/routines", HTTP_GET}, {"/api/routines", HTTP_POST},
            {"/api/companion/presence", HTTP_POST},
            {"/api/ui/screens", HTTP_GET}, {"/api/ui/screen", HTTP_GET}, {"/api/ui/screen", HTTP_POST},
            {"/api/ui/touch", HTTP_POST},
            {"/api/debug/motion", HTTP_GET}, {"/api/debug/servo-sample", HTTP_POST},
            {"/api/debug/logs", HTTP_GET},
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
            {"/api/debug/motion-test", HTTP_POST}, {"/api/debug/motion-test", HTTP_GET},
            {"/api/debug/audio-test", HTTP_POST}, {"/api/debug/audio-test", HTTP_GET},
            {"/api/debug/audio-files", HTTP_GET},
            {"/api/display/images", HTTP_GET},
            {"/api/display/image", HTTP_GET}, {"/api/display/image", HTTP_POST},
            {"/api/display/text", HTTP_GET}, {"/api/display/text", HTTP_POST},
            {"/api/display/rgb", HTTP_GET}, {"/api/display/rgb", HTTP_POST},
            {"/api/sensors", HTTP_GET},
#endif
            {"/api/events", HTTP_GET},
            {"/api/notification", HTTP_GET}, {"/api/notification", HTTP_POST},
            {"/api/notification/ack", HTTP_POST},
            {"/api/motion/config", HTTP_GET}, {"/api/motion/config", HTTP_POST},
            {"/api/camera", HTTP_GET}, {"/api/camera/image", HTTP_GET},
            {"/api/camera/capture", HTTP_POST}, {"/api/camera/save", HTTP_POST}
        };
        // Fail closed if server capacity or registration does not match the route table.
        if (sizeof(routes) / sizeof(routes[0]) != config.max_uri_handlers) {
            serverError = "http_route_capacity_mismatch";
            httpd_stop(server);
            server = nullptr;
            return;
        }
        for (const auto& route : routes) {
            httpd_uri_t entry{};
            entry.uri = route.path;
            entry.method = route.method;
            entry.handler = handle;
            entry.user_ctx = this;
            if (httpd_register_uri_handler(server, &entry) != ESP_OK) {
                serverError = "http_route_failed";
                httpd_stop(server);
                server = nullptr;
                return;
            }
        }
    }
    // Advance network lifecycle and stop the runtime if connectivity is lost.
    void tick(uint32_t now) {
        {
            std::lock_guard<std::mutex> lock(networkMutex);
            if (configurePending) {
                network.configure(pending, now);
                pending = {};
                configurePending = false;
            }
            network.tick(now);
            if (network.lostConnection) {
                companionPresence.update(false, now);
                uint32_t manualBoot = 0;
                {
                    std::lock_guard<std::mutex> deviceLock(device.mutex);
                    if (device.manualActive()) manualBoot = device.runtime.snapshot().bootId;
                    else device.runtime.stop(now);
                }
                if (manualBoot) device.requestStop(manualBoot);
            }
            if (network.state == NetworkState::Connected && radio.connected() &&
                static_cast<uint32_t>(WiFi.localIP()) != 0) {
                std::lock_guard<std::mutex> deviceLock(device.mutex);
                device.runtime.wifiReady(now);
            }
        }
        applyPendingDisplayBrightness();
        if (camera) {
            std::lock_guard<std::mutex> lock(device.mutex);
            camera->observeStop(device.runtime.stopEpoch());
        }
    }
    bool setup(uint32_t now) {
        std::lock_guard<std::mutex> lock(networkMutex);
        return network.setup(now);
    }
    const char* setupPassword() const {
        return network.ap ? radio.setupPassword() : nullptr;
    }
    void cancelSetup() {
        std::lock_guard<std::mutex> lock(networkMutex);
        pending = {};
        configurePending = false;
        network.cancelSetup();
    }
    void address(char* value, size_t length) {
        const String ip = network.ap ? WiFi.softAPIP().toString() :
            radio.connected() ? WiFi.localIP().toString() : String("0.0.0.0");
        snprintf(value, length, "%s", ip.c_str());
    }

private:
#ifdef NETBOT_TEST_BUILD
    friend struct OperationalEventTestAccess;
    friend struct DiagnosticHttpTestAccess;
#endif
    static std::unique_lock<std::mutex> httpLock(std::mutex& mutex) {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        return std::unique_lock<std::mutex>(mutex, std::try_to_lock);
#else
        return std::unique_lock<std::mutex>(mutex);
#endif
    }
    static esp_err_t busy(httpd_req_t* request) {
        return reply(request, 503, "{\"error\":\"device_busy\"}");
    }
    httpd_req_t* cameraImageRequest = nullptr;
    const uint8_t* cameraImageBytes = nullptr;
    size_t cameraImageLength = 0;
    uint32_t cameraImageBoot = 0, cameraImageCapture = 0;

    static esp_err_t cameraReceipt(httpd_req_t* request, int code, const CameraReceipt& value) {
        char json[512], error[48] = "null", saveError[48] = "null";
        if (value.error) snprintf(error, sizeof(error), "\"%s\"", value.error);
        if (value.saveError) snprintf(saveError, sizeof(saveError), "\"%s\"", value.saveError);
        const int size = snprintf(json, sizeof(json),
            "{\"version\":1,\"bootId\":%lu,\"requestId\":%lu,\"captureId\":%lu,"
            "\"state\":\"%s\",\"byteLength\":%lu,\"saveRequestId\":%lu,"
            "\"saveState\":\"%s\",\"error\":%s,\"saveError\":%s}",
            static_cast<unsigned long>(value.bootId), static_cast<unsigned long>(value.requestId),
            static_cast<unsigned long>(value.captureId), value.state,
            static_cast<unsigned long>(value.byteLength), static_cast<unsigned long>(value.saveRequestId),
            value.saveState, error, saveError);
        if (size <= 0 || static_cast<size_t>(size) >= sizeof(json))
            return reply(request, 503, "{\"error\":\"camera_encoding_failed\"}");
        return reply(request, code, json);
    }
    static bool cameraQuery(const char* uri, uint32_t* values, bool image) {
        const char* names[] = {"bootId", image ? "captureId" : "requestId", "captureId", "saveRequestId"};
        const unsigned count = image ? 2 : 4;
        const char* query = strchr(uri, '?');
        if (!query || strlen(++query) > 160) return false;
        unsigned seen = 0;
        while (*query) {
            unsigned index = 0;
            while (index < count && (strncmp(query, names[index], strlen(names[index])) ||
                    query[strlen(names[index])] != '=')) ++index;
            if (index == count || (seen & (1U << index))) return false;
            seen |= 1U << index;
            query += strlen(names[index]) + 1;
            if (*query < '0' || *query > '9') return false;
            uint32_t number = 0;
            do {
                const unsigned digit = *query++ - '0';
                if (number > (UINT32_MAX - digit) / 10) return false;
                number = number * 10 + digit;
            } while (*query >= '0' && *query <= '9');
            values[index] = number;
            if (!*query) break;
            if (*query++ != '&' || !*query) return false;
        }
        return seen == (1U << count) - 1 && values[0] && values[1];
    }
    esp_err_t cameraState(httpd_req_t* request) {
        uint32_t values[4]{};
        if (!cameraQuery(request->uri, values, false))
            return reply(request, 400, "{\"error\":\"invalid_camera_query\"}");
        if (cameraStartupError) return reply(request, 503, "{\"error\":\"camera_boot_init_failed\"}");
        if (!camera) return reply(request, 503, "{\"error\":\"camera_unavailable\"}");
        CameraReceipt value;
        bool contended = false;
        if (!camera->snapshot(values[0], values[1], values[2], values[3], value, true, &contended)) {
            if (contended) return busy(request);
            return reply(request, 409, "{\"error\":\"camera_identity_changed\"}");
        }
        return cameraReceipt(request, 200, value);
    }
    esp_err_t cameraRequest(httpd_req_t* request, const char* body, size_t length) {
        if (strchr(request->uri, '?') || !flatJson(body, length, 256))
            return reply(request, 400, "{\"error\":\"invalid_camera_request\"}");
        const bool save = pathIs(request->uri, "/api/camera/save");
        const char* captureKeys[] = {"bootId", "requestId"};
        const char* saveKeys[] = {"bootId", "captureId", "saveRequestId"};
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length + 1, &end, false);
        uint32_t boot = 0, capture = 0, id = 0;
        const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
            exactKeys(root, save ? saveKeys : captureKeys, save ? 3 : 2) &&
            unsignedField(root, "bootId", boot) &&
            unsignedField(root, save ? "saveRequestId" : "requestId", id) &&
            (!save || unsignedField(root, "captureId", capture));
        cJSON_Delete(root);
        if (!valid) return reply(request, 400, "{\"error\":\"invalid_camera_request\"}");
        if (cameraStartupError) return reply(request, 503, "{\"error\":\"camera_boot_init_failed\"}");
        if (!camera) return reply(request, 503, "{\"error\":\"camera_unavailable\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (device.audioTest && device.audioTest->resourceBusy())
            return reply(request, 409, "{\"error\":\"audio_busy\"}");
        if (device.imageDisplay && device.imageDisplay->resourceBusy())
            return reply(request, 409, "{\"error\":\"image_busy\"}");
        camera->observeStop(device.runtime.stopEpoch());
        const CameraAdmission result = save ? camera->save(boot, capture, id, true) :
            camera->capture(boot, id, true);
        lock.unlock();
        if (result.code == 202) return cameraReceipt(request, 202, result.receipt);
        char json[96];
        snprintf(json, sizeof(json), "{\"error\":\"%s\"}", result.error);
        return reply(request, result.code, json);
    }
    esp_err_t cameraImage(httpd_req_t* request) {
        uint32_t values[2]{};
        if (!cameraQuery(request->uri, values, true))
            return reply(request, 400, "{\"error\":\"invalid_camera_query\"}");
        if (cameraStartupError) return reply(request, 503, "{\"error\":\"camera_boot_init_failed\"}");
        if (!camera) return reply(request, 503, "{\"error\":\"camera_unavailable\"}");
        const uint8_t* bytes = nullptr;
        size_t length = 0;
        bool contended = false;
        if (!camera->holdImage(values[0], values[1], bytes, length, true, &contended)) {
            if (contended) return busy(request);
            return reply(request, 409, "{\"error\":\"camera_image_unavailable\"}");
        }
        cameraImageRequest = nullptr;
        if (httpd_req_async_handler_begin(request, &cameraImageRequest) != ESP_OK || !cameraImageRequest) {
            if (!camera->releaseImage(cameraImageRequest == nullptr, true))
                return reply(request, 503, "{\"error\":\"camera_cleanup_busy\"}");
            return reply(request, 503, "{\"error\":\"camera_image_admission_failed\"}");
        }
        cameraImageBytes = bytes;
        cameraImageLength = length;
        cameraImageBoot = values[0];
        cameraImageCapture = values[1];
        if (camera->startTask(cameraImageWorker, this)) return ESP_OK;
        const bool complete = httpd_req_async_handler_complete(cameraImageRequest) == ESP_OK;
        if (complete) cameraImageRequest = nullptr;
        if (!camera->releaseImage(complete, true))
            return reply(request, 503, "{\"error\":\"camera_cleanup_busy\"}");
        return reply(request, 503, "{\"error\":\"camera_task_unavailable\"}");
    }
    static void cameraImageWorker(void* pointer) {
        auto& self = *static_cast<Platform*>(pointer);
        char boot[16], capture[16];
        snprintf(boot, sizeof(boot), "%lu", static_cast<unsigned long>(self.cameraImageBoot));
        snprintf(capture, sizeof(capture), "%lu", static_cast<unsigned long>(self.cameraImageCapture));
        httpd_req_t* request = self.cameraImageRequest;
        const bool headers = httpd_resp_set_type(request, "image/jpeg") == ESP_OK &&
            httpd_resp_set_hdr(request, "Cache-Control", "no-store") == ESP_OK &&
            httpd_resp_set_hdr(request, "Connection", "close") == ESP_OK &&
            httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff") == ESP_OK &&
            httpd_resp_set_hdr(request, "X-Camera-Boot-Id", boot) == ESP_OK &&
            httpd_resp_set_hdr(request, "X-Camera-Capture-Id", capture) == ESP_OK;
        const esp_err_t sent = headers ? httpd_resp_send(request,
            reinterpret_cast<const char*>(self.cameraImageBytes),
            static_cast<int>(self.cameraImageLength)) : ESP_FAIL;
        if (sent != ESP_OK) ESP_LOGE("netbot-camera", "Image response failed");
        const bool complete = httpd_req_async_handler_complete(request) == ESP_OK;
        if (!complete) ESP_LOGE("netbot-camera", "Image request cleanup unconfirmed");
        if (complete) self.cameraImageRequest = nullptr;
        self.camera->releaseImage(complete);
        self.camera->finishTask();
    }

    Device& device;
    std::mutex networkMutex;
    std::mutex displayBrightnessMutex;
    Credentials pending;
    bool configurePending = false;
    bool displayBrightnessInitialized = false;
    bool displayBrightnessPending = false;
    InteractionOrigin displayBrightnessOrigin = InteractionOrigin::Unknown;
    uint8_t displayBrightness = kDisplayBrightnessDefault;
    uint8_t requestedDisplayBrightness = kDisplayBrightnessDefault;
    const char* displayBrightnessError = nullptr;
    CompanionPresence companionPresence;
    httpd_handle_t server = nullptr;
    bool logsEnabled = false;
    OperationalEventLog operationalEvents;

    static esp_err_t reply(httpd_req_t* request, int code, const char* body, const char* type = "application/json") {
        const char* status = code == 200 ? "200 OK" : code == 202 ? "202 Accepted" : code == 204 ? "204 No Content" :
            code == 400 ? "400 Bad Request" : code == 409 ? "409 Conflict" :
            code == 413 ? "413 Content Too Large" : code == 415 ? "415 Unsupported Media Type" :
            code == 422 ? "422 Unprocessable Content" : "503 Service Unavailable";
        httpd_resp_set_status(request, status);
        httpd_resp_set_type(request, type);
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        httpd_resp_set_hdr(request, "Connection", "close");
        httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
        return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    }
    static esp_err_t status(httpd_req_t* request, int code, const Snapshot& snapshot) {
        char json[1536];
        if (!statusJson(snapshot, json, sizeof(json)))
            return reply(request, 503, "{\"error\":\"status_encoding_failed\"}");
        return reply(request, code, json);
    }
    // The server callback recovers this Platform instance from the registered context.
    static esp_err_t handle(httpd_req_t* request) {
        return static_cast<Platform*>(request->user_ctx)->serve(request);
    }
    static esp_err_t page(httpd_req_t* request, const char* content) {
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        httpd_resp_set_hdr(request, "Connection", "close");
        if (httpd_resp_send_chunk(request, pageHead, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
            httpd_resp_send_chunk(request, content, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
            httpd_resp_send_chunk(request, "</main></body></html>", HTTPD_RESP_USE_STRLEN) != ESP_OK)
            return ESP_FAIL;
        return httpd_resp_send_chunk(request, nullptr, 0);
    }
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    esp_err_t audioTestError(httpd_req_t* request, int code, const char* error) {
        if (remoteLog) {
            char line[128];
            const int size = snprintf(line, sizeof(line), "[%lu] AUDIO_TEST ERROR error=%s",
                static_cast<unsigned long>(millis()), error);
            if (size > 0 && static_cast<size_t>(size) < sizeof(line)) remoteLog->append(line, size);
        }
        char json[96];
        snprintf(json, sizeof(json), "{\"error\":\"%s\"}", error);
        return reply(request, code, json);
    }

    esp_err_t audioTestRequest(httpd_req_t* request, const char* body, size_t length) {
        if (strchr(request->uri, '?') || !flatJson(body, length))
            return audioTestError(request, 400, "invalid_filename");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        const char* const keys[] = {"file"};
        const cJSON* field = root ? cJSON_GetObjectItemCaseSensitive(root, "file") : nullptr;
        const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
            exactKeys(root, keys, 1) && cJSON_IsString(field) && audioTestFilename(field->valuestring);
        if (!valid) {
            cJSON_Delete(root);
            return audioTestError(request, 400, "invalid_filename");
        }
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) { cJSON_Delete(root); return busy(request); }
        if (!device.audioTest) {
            cJSON_Delete(root);
            return audioTestError(request, 503, "audio_test_unavailable");
        }
        if (device.stopRequested || (camera && camera->isBusy()) ||
            (device.imageDisplay && device.imageDisplay->resourceBusy())) {
            cJSON_Delete(root);
            return audioTestError(request, 409, "audio_busy");
        }
        const char* error = device.audioTest->request(field->valuestring, device.runtime.isQuiet(), millis());
        cJSON_Delete(root);
        if (error) return audioTestError(request, 409, error);
        char json[160];
        snprintf(json, sizeof(json), "{\"accepted\":true,\"state\":\"queued\",\"bootId\":%lu,\"requestId\":%lu}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(device.audioTest->id()));
        return reply(request, 202, json);
    }

    esp_err_t audioTestState(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_audio_query\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (!device.audioTest) return reply(request, 503, "{\"error\":\"audio_test_unavailable\"}");
        const char* error = device.audioTest->error();
        char json[200];
        snprintf(json, sizeof(json),
            "{\"bootId\":%lu,\"requestId\":%lu,\"state\":\"%s\",\"error\":%s%s%s}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(device.audioTest->id()), device.audioTest->stateName(),
            error ? "\"" : "", error ? error : "null", error ? "\"" : "");
        return reply(request, 200, json);
    }

    esp_err_t audioFiles(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_audio_query\"}");
        AudioFileList files;
        bool ready = false;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            if (!device.audioTest) return reply(request, 503, "{\"error\":\"audio_test_unavailable\"}");
            ready = device.audioTest->takeFileList(files);
            if (!ready) {
                if (!device.audioTest->fileListRunning()) {
                    if (device.audioTest->active() || device.audioTest->speakerBusy() ||
                        (device.imageDisplay && device.imageDisplay->resourceBusy()) ||
                        device.stopRequested || (camera && camera->isBusy()))
                        return reply(request, 409, "{\"error\":\"audio_busy\"}");
                    if (!device.audioTest->startFileList())
                        return reply(request, 503, "{\"error\":\"audio_list_unavailable\"}");
                }
            }
        }
        if (ready) return audioFilesReply(request, files);
        const uint32_t started = millis();
        while (static_cast<uint32_t>(millis() - started) < 200) {
            if (device.audioTest->takeFileList(files)) return audioFilesReply(request, files);
            vTaskDelay(1);
        }
        return reply(request, 503, "{\"error\":\"audio_list_pending\"}");
    }

    esp_err_t rgbRequest(httpd_req_t* request, const char* body, size_t length) {
        if (strchr(request->uri, '?') || !flatJson(body, length))
            return reply(request, 400, "{\"error\":\"invalid_rgb_request\"}");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        const char* const keys[] = {"color", "durationMs"};
        const cJSON* color = root ? cJSON_GetObjectItemCaseSensitive(root, "color") : nullptr;
        const cJSON* duration = root ? cJSON_GetObjectItemCaseSensitive(root, "durationMs") : nullptr;
        const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
            exactKeys(root, keys, 2) && cJSON_IsString(color) &&
            cJSON_IsNumber(duration);
        if (!valid) {
            cJSON_Delete(root);
            return reply(request, 400, "{\"error\":\"invalid_rgb_request\"}");
        }
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) { cJSON_Delete(root); return busy(request); }
        if (!device.rgbController) {
            cJSON_Delete(root);
            return reply(request, 503, "{\"error\":\"rgb_unavailable\"}");
        }
        const bool busy = device.stopRequested || device.rgbEffect.stateName()[0] == 'q' ||
            device.rgbEffect.stateName()[0] == 'a' || device.rgbController->rgbActive();
        const char* error = device.rgbEffect.request(color->valuestring,
            static_cast<uint32_t>(duration->valueint), !device.stopRequested, busy,
            device.runtime.stopEpoch());
        cJSON_Delete(root);
        if (error) {
            char json[96];
            snprintf(json, sizeof(json), "{\"error\":\"%s\"}", error);
            const bool validationError = !strcmp(error, "invalid_rgb_color") ||
                !strcmp(error, "rgb_duration_invalid");
            return reply(request, validationError ? 400 : 409, json);
        }
        char json[160];
        snprintf(json, sizeof(json),
            "{\"accepted\":true,\"state\":\"queued\",\"bootId\":%lu,\"requestId\":%lu}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(device.rgbEffect.id()));
        return reply(request, 202, json);
    }

    esp_err_t rgbState(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_rgb_query\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        char json[200];
        snprintf(json, sizeof(json),
            "{\"bootId\":%lu,\"requestId\":%lu,\"state\":\"%s\",\"error\":%s%s%s}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(device.rgbEffect.id()),
            device.rgbEffect.stateName(),
            device.rgbEffect.error() ? "\"" : "", device.rgbEffect.error() ? device.rgbEffect.error() : "null",
            device.rgbEffect.error() ? "\"" : "");
        return reply(request, 200, json);
    }

    esp_err_t temporaryTextRequest(httpd_req_t* request, const char* body, size_t length) {
        if (strchr(request->uri, '?') || !flatJson(body, length))
            return reply(request, 400, "{\"error\":\"invalid_text_request\"}");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        const char* const keys[] = {"text"};
        const cJSON* field = root ? cJSON_GetObjectItemCaseSensitive(root, "text") : nullptr;
        const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
            exactKeys(root, keys, 1) && cJSON_IsString(field) &&
            temporaryTextValid(field->valuestring);
        if (!valid) {
            cJSON_Delete(root);
            return reply(request, 400, "{\"error\":\"invalid_text\"}");
        }
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) { cJSON_Delete(root); return busy(request); }
        if (!device.temporaryText) {
            cJSON_Delete(root);
            return reply(request, 503, "{\"error\":\"text_unavailable\"}");
        }
        const bool peripheralBusy = device.stopRequested ||
            (device.imageDisplay && device.imageDisplay->resourceBusy()) ||
            (device.audioTest && (device.audioTest->resourceBusy() || device.audioTest->speakerBusy())) ||
            (camera && !camera->sharedResourcesAvailable());
        const auto snapshot = device.runtime.snapshot();
        const char* error = device.temporaryText->request(field->valuestring, device.imageUiReady,
            peripheralBusy, device.runtime.stopEpoch());
        cJSON_Delete(root);
        if (error) {
            char json[96];
            snprintf(json, sizeof(json), "{\"error\":\"%s\"}", error);
            return reply(request, 409, json);
        }
        char json[160];
        snprintf(json, sizeof(json),
            "{\"accepted\":true,\"state\":\"queued\",\"bootId\":%lu,\"requestId\":%lu}",
            static_cast<unsigned long>(snapshot.bootId),
            static_cast<unsigned long>(device.temporaryText->id()));
        return reply(request, 202, json);
    }

    esp_err_t temporaryTextState(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_text_query\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (!device.temporaryText) return reply(request, 503, "{\"error\":\"text_unavailable\"}");
        const char* error = device.temporaryText->error();
        char json[200];
        snprintf(json, sizeof(json),
            "{\"bootId\":%lu,\"requestId\":%lu,\"state\":\"%s\",\"error\":%s%s%s}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(device.temporaryText->id()),
            device.temporaryText->stateName(),
            error ? "\"" : "", error ? error : "null", error ? "\"" : "");
        return reply(request, 200, json);
    }

    esp_err_t imageRequest(httpd_req_t* request, const char* body, size_t length) {
        if (strchr(request->uri, '?') || !flatJson(body, length))
            return reply(request, 400, "{\"error\":\"invalid_image_request\"}");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        const char* const keys[] = {"file"};
        const cJSON* field = root ? cJSON_GetObjectItemCaseSensitive(root, "file") : nullptr;
        const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
            exactKeys(root, keys, 1) && cJSON_IsString(field) && imageFilename(field->valuestring);
        if (!valid) {
            cJSON_Delete(root);
            return reply(request, 400, "{\"error\":\"invalid_filename\"}");
        }
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) { cJSON_Delete(root); return busy(request); }
        if (!device.imageDisplay) {
            cJSON_Delete(root);
            return reply(request, 503, "{\"error\":\"image_unavailable\"}");
        }
        const bool peripheralBusy = device.stopRequested || (camera && !camera->sharedResourcesAvailable()) ||
            (device.temporaryText && device.temporaryText->resourceBusy()) ||
            (device.audioTest && (device.audioTest->resourceBusy() || device.audioTest->speakerBusy()));
        const auto snapshot = device.runtime.snapshot();
        const char* error = device.imageDisplay->request(field->valuestring, device.imageUiReady,
            peripheralBusy, device.runtime.stopEpoch());
        cJSON_Delete(root);
        if (error) {
            char json[96];
            snprintf(json, sizeof(json), "{\"error\":\"%s\"}", error);
            return reply(request, 409, json);
        }
        char json[160];
        snprintf(json, sizeof(json), "{\"accepted\":true,\"state\":\"queued\",\"bootId\":%lu,\"requestId\":%lu}",
            static_cast<unsigned long>(snapshot.bootId), static_cast<unsigned long>(device.imageDisplay->id()));
        return reply(request, 202, json);
    }

    esp_err_t imageState(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_image_query\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (!device.imageDisplay) return reply(request, 503, "{\"error\":\"image_unavailable\"}");
        const char* error = device.imageDisplay->error();
        char json[200];
        snprintf(json, sizeof(json), "{\"bootId\":%lu,\"requestId\":%lu,\"state\":\"%s\",\"error\":%s%s%s}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(device.imageDisplay->id()), device.imageDisplay->stateName(),
            error ? "\"" : "", error ? error : "null", error ? "\"" : "");
        return reply(request, 200, json);
    }

    esp_err_t imageFiles(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_image_query\"}");
        ImageFileList files;
        bool ready = false;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            if (!device.imageDisplay) return reply(request, 503, "{\"error\":\"image_unavailable\"}");
            ready = device.imageDisplay->takeFileList(files);
            if (!ready && !device.imageDisplay->fileListRunning()) {
                if (device.imageDisplay->active() || device.stopRequested ||
                    (camera && !camera->sharedResourcesAvailable()) ||
                    (device.audioTest && (device.audioTest->resourceBusy() || device.audioTest->speakerBusy())))
                    return reply(request, 409, "{\"error\":\"image_busy\"}");
                if (!device.imageDisplay->startFileList())
                    return reply(request, 503, "{\"error\":\"image_list_unavailable\"}");
            }
        }
        if (!ready) return reply(request, 503, "{\"error\":\"image_list_pending\"}");
        if (files.error) {
            char json[96];
            snprintf(json, sizeof(json), "{\"error\":\"%s\"}", files.error);
            return reply(request, 503, json);
        }
        char json[1200];
        size_t used = static_cast<size_t>(snprintf(json, sizeof(json), "{\"files\":["));
        for (size_t i = 0; i < files.count; ++i) {
            const int count = snprintf(json + used, sizeof(json) - used, "%s\"%s\"",
                i ? "," : "", files.names[i]);
            if (count < 0 || static_cast<size_t>(count) >= sizeof(json) - used)
                return reply(request, 503, "{\"error\":\"image_list_encoding_failed\"}");
            used += static_cast<size_t>(count);
        }
        const int count = snprintf(json + used, sizeof(json) - used, "],\"truncated\":%s}",
            files.truncated ? "true" : "false");
        if (count < 0 || static_cast<size_t>(count) >= sizeof(json) - used)
            return reply(request, 503, "{\"error\":\"image_list_encoding_failed\"}");
        return reply(request, 200, json);
    }

    static esp_err_t audioFilesReply(httpd_req_t* request, const AudioFileList& files) {
        if (files.error) {
            char error[96];
            snprintf(error, sizeof(error), "{\"error\":\"%s\"}", files.error);
            return reply(request, 503, error);
        }
        char json[1200];
        size_t used = static_cast<size_t>(snprintf(json, sizeof(json), "{\"files\":["));
        for (size_t i = 0; i < files.count; ++i) {
            const int count = snprintf(json + used, sizeof(json) - used, "%s\"%s\"",
                i ? "," : "", files.names[i]);
            if (count < 0 || static_cast<size_t>(count) >= sizeof(json) - used)
                return reply(request, 503, "{\"error\":\"audio_list_encoding_failed\"}");
            used += static_cast<size_t>(count);
        }
        const int count = snprintf(json + used, sizeof(json) - used, "],\"truncated\":%s}",
            files.truncated ? "true" : "false");
        if (count < 0 || static_cast<size_t>(count) >= sizeof(json) - used)
            return reply(request, 503, "{\"error\":\"audio_list_encoding_failed\"}");
        return reply(request, 200, json);
    }

    esp_err_t motionTestState(httpd_req_t* request) {
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (!device.motionTest) return reply(request, 503, "{\"error\":\"motion_test_unavailable\"}");
        const auto& test = *device.motionTest;
        using State = BspMotionTest::State;
        const auto requestId = test.requestId();
        const auto state = test.state();
        const auto action = test.action();
        const char* name = state == State::Preparing ? "preparing" :
            state == State::Running ? "running" : state == State::Complete ? "complete" :
            state == State::Stopped ? "stopped" : state == State::Failed ? "failed" :
            state == State::Rejected ? "rejected" : state == State::Uncertain ? "uncertain" : "idle";
        const char* error = test.failure();
        if (requestId != test.requestId()) return busy(request);
        const bool available = device.motionTestReady && !device.manualActive() &&
            !device.stopRequested && !device.bspYawStop.load() &&
            device.pendingMotionTest == Device::MotionTestAction::None &&
            (state == State::Idle || state == State::Complete || state == State::Rejected);
        char json[384];
        snprintf(json, sizeof(json),
            "{\"bootId\":%lu,\"requestId\":%lu,\"admissionReady\":%s,\"directionReferenceReady\":%s,"
            "\"action\":\"%s\",\"state\":\"%s\",\"error\":%s%s%s,"
            "\"gestureSubmitted\":%s,\"physicalResult\":\"unverified\"}",
            static_cast<unsigned long>(device.runtime.snapshot().bootId),
            static_cast<unsigned long>(requestId), available ? "true" : "false",
            test.directionReferenceReady() ? "true" : "false",
            BspMotionTest::actionName(action), name, error ? "\"" : "",
            error ? error : "null", error ? "\"" : "",
            test.gestureSubmitted() ? "true" : "false");
        return reply(request, 200, json);
    }

    esp_err_t motionTestRequest(httpd_req_t* request, const char* body, size_t length) {
        if (!flatJson(body, length)) return reply(request, 400, "{\"error\":\"invalid_motion_test\"}");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        const cJSON* field = root ? cJSON_GetObjectItemCaseSensitive(root, "action") : nullptr;
        const cJSON* value = root ? cJSON_GetObjectItemCaseSensitive(root, "value") : nullptr;
        const cJSON* xField = root ? cJSON_GetObjectItemCaseSensitive(root, "x") : nullptr;
        const cJSON* yField = root ? cJSON_GetObjectItemCaseSensitive(root, "y") : nullptr;
        const cJSON* speedField = root ? cJSON_GetObjectItemCaseSensitive(root, "speed") : nullptr;
        const char* const keys[] = {"action", value ? "value" : "speed", "speed"};
        const char* const xyKeys[] = {"action", "x", "y", "speed"};
        using Action = Device::MotionTestAction;
        Action action = Action::None;
        const bool validJson = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
            cJSON_IsString(field) && field->valuestring;
        const bool moveXYRequest = validJson && !strcmp(field->valuestring, "move_xy");
        const bool validKeys = validJson && (moveXYRequest ?
            exactKeys(root, xyKeys, 3 + (speedField != nullptr)) :
            exactKeys(root, keys, 1 + (value != nullptr) + (speedField != nullptr)));
        if (validKeys) {
            if (!strcmp(field->valuestring, "original_smoke")) action = Action::OriginalSmoke;
            else if (!strcmp(field->valuestring, "original_035")) action = Action::Original035;
            else if (!strcmp(field->valuestring, "head_left")) action = Action::HeadLeft;
            else if (!strcmp(field->valuestring, "head_right")) action = Action::HeadRight;
            else if (!strcmp(field->valuestring, "head_up")) action = Action::HeadUp;
            else if (!strcmp(field->valuestring, "head_down")) action = Action::HeadDown;
            else if (!strcmp(field->valuestring, "head_up_left")) action = Action::HeadUpLeft;
            else if (!strcmp(field->valuestring, "head_up_right")) action = Action::HeadUpRight;
            else if (!strcmp(field->valuestring, "head_down_left")) action = Action::HeadDownLeft;
            else if (!strcmp(field->valuestring, "head_down_right")) action = Action::HeadDownRight;
            else if (!strcmp(field->valuestring, "head_center")) action = Action::HeadCenter;
            else if (!strcmp(field->valuestring, "head_stop")) action = Action::HeadStop;
            else if (!strcmp(field->valuestring, "home_capture")) action = Action::HomeCapture;
            else if (!strcmp(field->valuestring, "home_go")) action = Action::HomeGo;
            else if (!strcmp(field->valuestring, "move_x")) action = Action::MoveX;
            else if (!strcmp(field->valuestring, "move_y")) action = Action::MoveY;
            else if (moveXYRequest) action = Action::MoveXY;
            else if (!strcmp(field->valuestring, "nod_yes")) action = Action::NodYes;
            else if (!strcmp(field->valuestring, "shake_no")) action = Action::ShakeNo;
            else if (!strcmp(field->valuestring, "dance")) action = Action::Dance;
            else if (!strcmp(field->valuestring, "grumpy_shake")) action = Action::GrumpyShake;
        }
        std::optional<int> offset;
        std::optional<int> x, y;
        std::optional<int> speed;
        const char* error = nullptr;
        if (action == Action::MoveXY) {
            if (!cJSON_IsNumber(xField) || !std::isfinite(xField->valuedouble) ||
                std::floor(xField->valuedouble) != xField->valuedouble ||
                !cJSON_IsNumber(yField) || !std::isfinite(yField->valuedouble) ||
                std::floor(yField->valuedouble) != yField->valuedouble)
                error = "{\"error\":\"integer_x_y_required\"}";
            else if (!BspMotionTest::validOffset(Action::MoveX, xField->valuedouble))
                error = "{\"error\":\"x_out_of_range\",\"min\":-1280,\"max\":1280}";
            else if (!BspMotionTest::validOffset(Action::MoveY, yField->valuedouble))
                error = "{\"error\":\"y_out_of_range\",\"min\":0,\"max\":900}";
            else {
                x = xField->valueint;
                y = yField->valueint;
            }
        } else if (BspMotionTest::isOffset(action)) {
            if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
                std::floor(value->valuedouble) != value->valuedouble)
                error = "{\"error\":\"integer_value_required\"}";
            else if (!BspMotionTest::validOffset(action, value->valuedouble))
                error = action == Action::MoveX ?
                    "{\"error\":\"value_out_of_range\",\"min\":-1280,\"max\":1280}" :
                    "{\"error\":\"value_out_of_range\",\"min\":0,\"max\":900}";
            else offset = value->valueint;
        } else if (BspMotionTest::isDirection(action) && value) {
            if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
                std::floor(value->valuedouble) != value->valuedouble)
                error = "{\"error\":\"integer_value_required\"}";
            else if (value->valuedouble < 1 || value->valuedouble > BspMotionTest::maxDirectionStep(action))
                error = "{\"error\":\"direction_step_out_of_range\"}";
            else offset = value->valueint;
        } else if (value) {
            error = "{\"error\":\"value_only_for_move_x_or_move_y\"}";
        }
        if (!error && speedField) {
            if (!BspMotionTest::supportsSpeed(action))
                error = "{\"error\":\"speed_only_for_move_x_move_y_move_xy_or_home_go\"}";
            else if (!cJSON_IsNumber(speedField) || !std::isfinite(speedField->valuedouble) ||
                std::floor(speedField->valuedouble) != speedField->valuedouble)
                error = "{\"error\":\"integer_speed_required\"}";
            else if (speedField->valuedouble < 0 || speedField->valuedouble > BspMotionTest::maxSpeed)
                error = "{\"error\":\"speed_out_of_range\",\"min\":0,\"max\":1000}";
            else speed = speedField->valueint;
        }
        cJSON_Delete(root);
        if (action == Action::None) return reply(request, 400, "{\"error\":\"invalid_motion_test\"}");
        if (error) return reply(request, 400, error);
        const char* name = BspMotionTest::actionName(action);
        auto networkLock = httpLock(networkMutex);
        if (!networkLock.owns_lock()) return busy(request);
        if (network.ap || network.state != NetworkState::Connected || !radio.connected() ||
            static_cast<uint32_t>(WiFi.localIP()) == 0)
            return reply(request, 503, "{\"error\":\"station_unavailable\"}");
        auto deviceLock = httpLock(device.mutex);
        if (!deviceLock.owns_lock()) return busy(request);
        if (!device.motionTest) return reply(request, 503, "{\"error\":\"motion_test_unavailable\"}");
        if (!device.motionTestReady || device.manualActive() || device.stopRequested ||
            device.pendingMotionTest != Action::None)
            return reply(request, 409, "{\"error\":\"motion_test_busy_or_unavailable\"}");
        const bool reserved = action == Action::MoveXY ?
            device.motionTest->reserveMoveXY(*x, *y, speed) : device.motionTest->reserve(action, offset, speed);
        if (!reserved)
            return reply(request, 409, "{\"error\":\"motion_test_busy_or_unavailable\"}");
        device.pendingMotionTest = action;
        const uint32_t boot = device.runtime.snapshot().bootId;
        const uint32_t requestId = device.motionTest->requestId();
        if (remoteLog) {
            char line[160];
            const auto time = static_cast<unsigned long>(millis());
            const int size = action == Action::MoveXY ? snprintf(line, sizeof(line),
                "[%lu] MOTION_TEST ADMITTED source=http_test experiment=%s requested_x=%d requested_y=%d "
                "speed=%d units=tenths_of_degree",
                time, name, *x, *y, speed.value_or(BspMotionTest::defaultSpeed)) : offset ? snprintf(line, sizeof(line),
                "[%lu] MOTION_TEST ADMITTED source=http_test experiment=%s requested_offset=%d units=tenths_of_degree",
                time, name, *offset) : snprintf(line, sizeof(line),
                "[%lu] MOTION_TEST ADMITTED source=http_test experiment=%s", time, name);
            if (size > 0 && static_cast<size_t>(size) < sizeof(line)) remoteLog->append(line, size);
        }
        deviceLock.unlock();
        networkLock.unlock();
        char json[192];
        snprintf(json, sizeof(json),
            "{\"accepted\":true,\"action\":\"%s\",\"bootId\":%lu,\"requestId\":%lu,\"state\":\"queued\","
            "\"physicalResult\":\"unverified\"}", name, static_cast<unsigned long>(boot),
            static_cast<unsigned long>(requestId));
        return reply(request, 202, json);
    }
#endif
    // Dispatch reads to snapshots and validate bounded JSON before processing writes.
    esp_err_t serve(httpd_req_t* request) {
        if (request->method == HTTP_GET) {
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
            if (pathIs(request->uri, "/api/debug/audio-test")) return audioTestState(request);
            if (pathIs(request->uri, "/api/debug/motion-test")) return motionTestState(request);
            if (pathIs(request->uri, "/api/debug/audio-files")) return audioFiles(request);
            if (pathIs(request->uri, "/api/display/images")) return imageFiles(request);
            if (pathIs(request->uri, "/api/display/image")) return imageState(request);
            if (pathIs(request->uri, "/api/display/text")) return temporaryTextState(request);
            if (pathIs(request->uri, "/api/display/rgb")) return rgbState(request);
            if (pathIs(request->uri, "/api/sensors")) return sensorState(request);
#endif
            if (pathIs(request->uri, "/api/camera")) return cameraState(request);
            if (pathIs(request->uri, "/api/camera/image")) return cameraImage(request);
            if (pathIs(request->uri, "/api/motion/config")) return motionConfigurationRequest(request);
            if (pathIs(request->uri, "/api/events")) return events(request);
            if (pathIs(request->uri, "/api/notification")) {
                if (strchr(request->uri, '?'))
                    return reply(request, 400, "{\"error\":\"invalid_notification_query\"}");
                return notificationState(request, 200);
            }
            if (pathIs(request->uri, "/api/debug/logs")) return logs(request);
            if (pathIs(request->uri, "/api/debug/motion")) return motionDebug(request);
            if (pathIs(request->uri, "/api/ui/screens")) return uiScreens(request);
            if (pathIs(request->uri, "/api/ui/screen")) return uiScreen(request);
            if (pathIs(request->uri, "/api/status") || pathIs(request->uri, "/api/motion") ||
                pathIs(request->uri, "/api/routines")) {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
                Snapshot value;
                if (!device.trySnapshot(value)) return busy(request);
                return status(request, 200, value);
#else
                return status(request, 200, device.snapshot());
#endif
            }
            if (pathIs(request->uri, "/api/display/brightness")) return displayBrightnessStatus(request, 200);
            if (pathIs(request->uri, "/api/network")) return networkStatus(request);
            if (pathIs(request->uri, "/api/version")) {
                char json[96];
                if (!versionJson(json, sizeof(json)))
                    return reply(request, 503, "{\"error\":\"version_encoding_failed\"}");
                return reply(request, 200, json);
            }
            if (pathIs(request->uri, "/setup")) {
                bool active;
                {
                    auto lock = httpLock(networkMutex);
                    if (!lock.owns_lock()) return busy(request);
                    active = network.ap;
                }
                if (!active) return reply(request, 409, "{\"error\":\"setup_not_active\"}");
                return page(request, setupPage);
            }
            return page(request, diagnosticPage);
        }
        const bool configRoute = pathIs(request->uri, "/api/motion/config");
        const bool cameraRoute = pathIs(request->uri, "/api/camera/capture") ||
            pathIs(request->uri, "/api/camera/save");
        const bool notificationRoute = pathIs(request->uri, "/api/notification") ||
            pathIs(request->uri, "/api/notification/ack");
        const size_t bodyLimit = cameraRoute ? 256 : notificationRoute ? kNotificationBodyBytes :
            configRoute ? kMaximumConfigBodyBytes : kMaximumBodyBytes;
        if (request->content_len > bodyLimit) return reply(request, 413, "{\"error\":\"body_too_large\"}");
        char type[48]{};
        if (httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK ||
            (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8")))
            return reply(request, 415, "{\"error\":\"json_required\"}");
        char* body = requestBody;
        size_t read = 0;
        const uint32_t began = millis();
        while (read < request->content_len) {
            const int count = httpd_req_recv(request, body + read, request->content_len - read);
            if (count <= 0 || millis() - began >= 1000) {
                if (configRoute) memset(body, 0, kMaximumConfigBodyBytes + 1);
                return reply(request, 400, "{\"error\":\"incomplete_body\"}");
            }
            read += count;
        }
        body[read] = '\0';
        if (cameraRoute) return cameraRequest(request, body, read);
        if (notificationRoute) return notificationRequest(request, body, read);
        if (configRoute) {
            const auto result = motionConfigurationRequest(request, body, read);
            memset(body, 0, kMaximumConfigBodyBytes + 1);
            configRequest = ConfigurationRequest{};
            return result;
        }
        if (pathIs(request->uri, "/api/display/brightness"))
            return setDisplayBrightness(request, body, read);
        if (pathIs(request->uri, "/api/config")) return configure(request, body, read);
        if (pathIs(request->uri, "/api/debug/servo-sample")) return servoSample(request, body, read);
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
        if (pathIs(request->uri, "/api/debug/motion-test")) return motionTestRequest(request, body, read);
        if (pathIs(request->uri, "/api/debug/audio-test")) return audioTestRequest(request, body, read);
        if (pathIs(request->uri, "/api/display/image")) return imageRequest(request, body, read);
        if (pathIs(request->uri, "/api/display/text")) return temporaryTextRequest(request, body, read);
        if (pathIs(request->uri, "/api/display/rgb")) return rgbRequest(request, body, read);
#endif
        if (pathIs(request->uri, "/api/ui/screen")) return uiNavigate(request, body, read);
        if (pathIs(request->uri, "/api/ui/touch")) return uiTouch(request, body, read);
        if (pathIs(request->uri, "/api/companion/presence")) {
            bool connected = false;
            if (!parseCompanionPresence(body, read, connected))
                return reply(request, 400, "{\"error\":\"invalid_presence\"}");
            companionPresence.update(connected, millis());
            char acknowledgement[64];
            snprintf(acknowledgement, sizeof(acknowledgement),
                "{\"connected\":%s,\"expiresInMs\":%lu}", connected ? "true" : "false",
                static_cast<unsigned long>(connected ? kCompanionPresenceExpiryMs : 0));
            return reply(request, 200, acknowledgement);
        }
        const bool stop = pathIs(request->uri, "/api/stop");
        const ParsedRequest parsed = parseRequest(body, read, stop);
        if (!parsed.valid) return reply(request, 400, "{\"error\":\"invalid_command\"}");
        if ((pathIs(request->uri, "/api/motion") && !motionAction(parsed.command.action)) ||
            (pathIs(request->uri, "/api/routines") && !routineAction(parsed.command.action)))
            return reply(request, 400, "{\"error\":\"action_route_mismatch\"}");
        if (stop) {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
            const auto admitted = device.requestDiagnosticStop(parsed.command.bootId);
            if (admitted.status >= 400) return reply(request, admitted.status, "{\"error\":\"boot_changed\"}");
            if (camera) camera->cancel();
            const uint32_t rail = device.diagnosticRailResult();
            const uint32_t state = rail & 0xff;
            const uint32_t evidence = rail >> 8;
            const uint8_t torque = device.diagnosticTorqueResult();
            const bool failed = state == 5 || torque == 2;
            const bool finished = !failed && state == 4 && torque == 1;
            char json[320];
            snprintf(json, sizeof(json),
                "{\"stopReceipt\":{\"bootId\":%lu,\"stopId\":1,\"outcome\":\"%s\"},"
                "\"rail\":{\"state\":\"%s\",\"lowPossible\":%s,\"lowAccepted\":%s,"
                "\"directionPossible\":%s,\"directionAccepted\":%s,\"cleanupUnknown\":%s},"
                "\"torque\":\"%s\",\"physicalStop\":\"unverified\"}",
                static_cast<unsigned long>(device.diagnosticStopBoot()),
                failed ? "failed" : finished ? "unconfirmed" : "pending",
                state == 4 ? "request_accepted" : state == 5 ? "failed" : "pending",
                evidence & 1 ? "true" : "false", evidence & 2 ? "true" : "false",
                evidence & 4 ? "true" : "false", evidence & 8 ? "true" : "false",
                evidence & 16 ? "true" : "false",
                torque == 1 ? "acknowledged" : torque == 2 ? "unconfirmed" : "pending");
            return reply(request, failed ? 503 : finished ? 200 : 202, json);
#else
            StopReceipt identity{};
            const auto admitted = device.requestStop(parsed.command.bootId, &identity);
            if (admitted.status >= 400) {
                char error[64];
                snprintf(error, sizeof(error), "{\"error\":\"%s\"}", admitted.code);
                return reply(request, admitted.status, error);
            }
            if (remoteLog) {
                char line[160];
                const int size = snprintf(line, sizeof(line),
                    "[%lu] MOTION_TEST STOP_LATCHED source=http physical=unverified",
                    static_cast<unsigned long>(millis()));
                if (size > 0 && static_cast<size_t>(size) < sizeof(line)) remoteLog->append(line, size);
            }
            if (camera) camera->cancel();
            if (admitted.execute) {
                recordOperationalEvent(millis(), OperationalEventKind::Stop, OperationalStatus::Requested,
                    OperationalAction::Stop, OperationalError::None, identity.stopId);
            }
            const uint32_t beganStop = millis();
            for (;;) {
                const auto stopped = device.stopStatus();
                const auto receipt = stopped.stopReceipt;
                if (!receipt.outcome || receipt.bootId != identity.bootId || receipt.stopId != identity.stopId)
                    return reply(request, 409, "{\"error\":\"stop_receipt_superseded\"}");
                const bool confirmed = !strcmp(receipt.outcome, "confirmed");
                const bool failed = !strcmp(receipt.outcome, "failed");
                if (confirmed || failed || millis() - beganStop >= 250) {
                    if (admitted.execute)
                        recordOperationalEvent(millis(), OperationalEventKind::Stop,
                            confirmed ? OperationalStatus::Completed :
                            failed ? OperationalStatus::Failed : OperationalStatus::Pending,
                            OperationalAction::Stop, failed ? OperationalError::Failed : OperationalError::None,
                            identity.stopId);
                    if (failed) {
                        char json[192];
                        snprintf(json, sizeof(json),
                            "{\"error\":\"stop_unconfirmed\",\"stopReceipt\":{\"bootId\":%lu,"
                            "\"stopId\":%lu,\"outcome\":\"failed\"}}",
                            static_cast<unsigned long>(receipt.bootId),
                            static_cast<unsigned long>(receipt.stopId));
                        return reply(request, 503, json);
                    }
                    return status(request, confirmed ? 200 : 202, stopped);
                }
                vTaskDelay(1);
            }
#endif
        }
        Snapshot receipt{};
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        Admission admitted{};
        if (!device.tryAdmit(parsed.command, receipt, admitted)) return busy(request);
#else
        const Admission admitted = device.admit(parsed.command, &receipt);
#endif
        if (admitted.execute) {
            recordOperationalEvent(millis(), OperationalEventKind::Action, OperationalStatus::Requested,
                operationalActionFor(parsed.command.action), OperationalError::None, parsed.command.commandId);
        } else if (admitted.status >= 400) {
            recordOperationalEvent(millis(), OperationalEventKind::Action, OperationalStatus::Failed,
                operationalActionFor(parsed.command.action), operationalErrorFor(admitted.code),
                parsed.command.commandId);
        }
        if (admitted.status == 200 || admitted.status == 202) return status(request, admitted.status, receipt);
        char error[96];
        snprintf(error, sizeof(error), "{\"error\":\"%s\"}", admitted.code);
        return reply(request, admitted.status, error);
    }
    esp_err_t displayBrightnessStatus(httpd_req_t* request, int code) {
        DisplayBrightnessStatus state;
        bool contended = false;
        if (!displayBrightnessStatus(state, true, &contended))
            return contended ? busy(request) : reply(request, 503, "{\"error\":\"brightness_unavailable\"}");
        return displayBrightnessResponse(request, code, state);
    }
    esp_err_t displayBrightnessResponse(httpd_req_t* request, int code,
                                        const DisplayBrightnessStatus& state) {
        char json[192];
        if (!displayBrightnessJson(state, json, sizeof(json)))
            return reply(request, 503, "{\"error\":\"brightness_encoding_failed\"}");
        return reply(request, code, json);
    }
    esp_err_t notificationState(httpd_req_t* request, int code) {
        NotificationSnapshot state;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            device.notification.advance(millis(), companionPresent(millis()));
            state = device.notification.snapshot(millis());
        }
        return notificationReply(request, code, state);
    }
    esp_err_t notificationReply(httpd_req_t* request, int code, const NotificationSnapshot& state) {
        if (!state.identity.bootId) return reply(request, 503, "{\"error\":\"notification_unavailable\"}");
        Json json(uiJson, sizeof(uiJson));
        json.add("{\"schemaVersion\":1,\"demoVersion\":%u,\"arrivalCue\":\"%s\","
            "\"snapshotKind\":\"notification\",\"bootId\":%lu,"
            "\"notificationId\":%lu,\"revision\":%lu,\"templateId\":\"%s\",\"title\":\"%s\",\"detail\":\"%s\","
            "\"state\":\"%s\",\"active\":%s,\"remainingMs\":%lu,\"avatarRestored\":%s,"
            "\"interaction\":{\"interactionId\":%lu,\"revision\":%lu,\"action\":\"%s\","
            "\"status\":\"%s\",\"error\":",
            static_cast<unsigned>(kNotificationDemoVersion), notificationCueName(state.arrivalCue),
            static_cast<unsigned long>(state.identity.bootId),
            static_cast<unsigned long>(state.identity.notificationId),
            static_cast<unsigned long>(state.identity.revision), state.templateId, state.title, state.detail,
            notificationStateName(state.state), state.active ? "true" : "false",
            static_cast<unsigned long>(state.remainingMs), state.avatarRestored ? "true" : "false",
            static_cast<unsigned long>(state.interactionId), static_cast<unsigned long>(state.interactionRevision),
            state.interactionId ? "open_in_companion" : "", notificationReceiptName(state.receipt));
        if (state.error) json.add("\"%s\"", state.error);
        else json.add("null");
        json.add("}}");
        return json.ok ? reply(request, code, uiJson) :
            reply(request, 503, "{\"error\":\"notification_encoding_failed\"}");
    }
    esp_err_t notificationRequest(httpd_req_t* request, const char* body, size_t length) {
        const bool ack = pathIs(request->uri, "/api/notification/ack");
        NotificationRequest create;
        NotificationAcknowledgement receipt;
        if (strchr(request->uri, '?') ||
            !(ack ? parseNotificationAck(body, length, receipt) : parseNotification(body, length, create)))
            return reply(request, 400, ack ? "{\"error\":\"invalid_notification_ack\"}" :
                "{\"error\":\"invalid_notification\"}");
        Admission result;
        NotificationSnapshot state;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            const uint32_t now = millis();
            const bool receiver = companionPresent(now);
            device.notification.advance(now, receiver);
            result = ack ? device.notification.acknowledge(receipt.bootId, receipt.notificationId,
                receipt.interactionId, receipt.revision, receipt.completed, now, receiver) :
                device.notification.receive(create.bootId, create.notificationId, create.templateId, now);
            state = device.notification.snapshot(now);
        }
        if (result.status < 400) return notificationReply(request, result.status, state);
        char error[96];
        snprintf(error, sizeof(error), "{\"error\":\"%s\"}", result.code);
        return reply(request, result.status, error);
    }
    esp_err_t setDisplayBrightness(httpd_req_t* request, const char* body, size_t length) {
        uint8_t requested = 0;
        const auto parsed = parseDisplayBrightnessRequest(body, length, requested);
        if (parsed == DisplayBrightnessRequestResult::Invalid)
            return reply(request, 400, "{\"error\":\"invalid_brightness\"}");
        if (parsed == DisplayBrightnessRequestResult::OutOfRange)
            return reply(request, 422, "{\"error\":\"brightness_out_of_range\"}");
        DisplayBrightnessStatus state;
        const auto queued = requestDisplayBrightness(requested, InteractionOrigin::Remote, true, &state);
        if (queued == DisplayBrightnessQueueResult::Unavailable)
            return reply(request, 503, "{\"error\":\"brightness_unavailable\"}");
        if (queued == DisplayBrightnessQueueResult::Busy)
            return reply(request, 409, "{\"error\":\"brightness_busy\"}");
        if (queued == DisplayBrightnessQueueResult::OutOfRange)
            return reply(request, 422, "{\"error\":\"brightness_out_of_range\"}");
        return displayBrightnessResponse(request, 202, state);
    }
    void applyPendingDisplayBrightness() {
        uint8_t requested = 0;
        InteractionOrigin origin;
        {
            std::lock_guard<std::mutex> lock(displayBrightnessMutex);
            if (!displayBrightnessInitialized || !displayBrightnessPending) return;
            requested = requestedDisplayBrightness;
            origin = displayBrightnessOrigin;
        }
        const uint8_t before = M5.Display.getBrightness();
        M5.Display.setBrightness(requested);
        const uint8_t readback = M5.Display.getBrightness();
        {
            std::lock_guard<std::mutex> lock(displayBrightnessMutex);
            displayBrightness = readback;
            displayBrightnessError = readback == requested ? nullptr : "brightness_readback_mismatch";
            recordOperationalEvent(millis(), OperationalEventKind::Brightness,
                readback == requested ? OperationalStatus::Completed : OperationalStatus::Failed,
                OperationalAction::None,
                readback == requested ? OperationalError::None : OperationalError::Failed,
                0, 0, 0, {origin, OperationalMetadata::Requested | OperationalMetadata::Before |
                    OperationalMetadata::After, requested, before, readback});
            displayBrightnessPending = false;
        }
    }
    esp_err_t networkStatus(httpd_req_t* request) {
        NetworkState state;
        const char* error;
        bool setup;
        {
            // A submitted form is "connecting" before the loop task has started the attempt.
            auto lock = httpLock(networkMutex);
            if (!lock.owns_lock()) return busy(request);
            state = configurePending ? NetworkState::Connecting : network.state;
            error = configurePending ? nullptr : network.error;
            setup = network.ap;
        }
        char json[128];
        if (!networkJson(state, setup, error, json, sizeof(json)))
            return reply(request, 503, "{\"error\":\"status_encoding_failed\"}");
        return reply(request, 200, json);
    }
    esp_err_t configure(httpd_req_t* request, const char* body, size_t length) {
        if (!flatJson(body, length)) return reply(request, 400, "{\"error\":\"invalid_config\"}");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
        const cJSON* password = cJSON_GetObjectItemCaseSensitive(root, "password");
        Credentials value;
        bool valid = root && end == body + length && cJSON_GetArraySize(root) == 2 &&
            cJSON_IsString(ssid) && cJSON_IsString(password) &&
            strlen(ssid->valuestring) < sizeof(value.ssid) && strlen(password->valuestring) < sizeof(value.password);
        if (valid) {
            strcpy(value.ssid, ssid->valuestring);
            strcpy(value.password, password->valuestring);
            valid = value.valid();
        }
        cJSON_Delete(root);
        if (!valid) return reply(request, 400, "{\"error\":\"invalid_config\"}");
        bool ready;
        {
            auto lock = httpLock(networkMutex);
            if (!lock.owns_lock()) return busy(request);
            ready = network.ap && !configurePending && network.state != NetworkState::Connecting;
            if (ready) { pending = value; configurePending = true; }
        }
        if (!ready) return reply(request, 409, "{\"error\":\"setup_not_ready\"}");
        return reply(request, 202, "{\"state\":\"pending_connection\"}");
    }
    static bool pathIs(const char* uri, const char* path) {
        const size_t length = strlen(path);
        return !strncmp(uri, path, length) && (!uri[length] || uri[length] == '?');
    }

    // Remote UI: HTTP records one request; loop() runs it through the same handler as a touch.
    struct Json {
        char* out; size_t capacity, length = 0; bool ok = true;
        Json(char* out, size_t capacity) : out(out), capacity(capacity) { out[0] = '\0'; }
        void add(const char* format, ...) __attribute__((format(printf, 2, 3))) {
            if (!ok) return;
            va_list args;
            va_start(args, format);
            const int count = vsnprintf(out + length, capacity - length, format, args);
            va_end(args);
            if (count < 0 || static_cast<size_t>(count) >= capacity - length) ok = false;
            else length += count;
        }
    };
    esp_err_t sensorState(httpd_req_t* request) {
        if (strchr(request->uri, '?')) return reply(request, 400, "{\"error\":\"invalid_sensor_query\"}");
        SensorDiagnostics s;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            s = device.sensors;
        }
        if (!s.published) return reply(request, 503, "{\"error\":\"sensors_not_ready\"}");
        const uint32_t now = millis();
        const uint32_t topAge = now - s.topAt, lightAge = now - s.lightAt;
        const auto boolean = [](bool v) { return v ? "true" : "false"; };
        Json json(uiJson, sizeof(uiJson));
        const auto error = [&](const char* value) {
            if (value) json.add("\"%s\"", value); else json.add("null");
        };
        json.add("{\"top\":{\"initialized\":%s,\"available\":%s,\"error\":",
            boolean(s.topInitialized), boolean(s.topAvailable));
        error(s.topError);
        json.add(",\"hasSample\":%s,\"fresh\":%s,\"ageMs\":",
            boolean(s.topValid), boolean(s.topValid && s.topAvailable && !s.topError && topAge <= 350));
        if (s.topValid) json.add("%lu,\"front\":%u,\"middle\":%u,\"back\":%u",
            static_cast<unsigned long>(topAge), s.front, s.middle, s.back);
        else json.add("null,\"front\":null,\"middle\":null,\"back\":null");
        json.add("},\"light\":{\"initialized\":%s,\"available\":%s,\"error\":",
            boolean(s.lightInitialized), boolean(s.lightAvailable));
        error(s.lightError);
        json.add(",\"hasSample\":%s,\"fresh\":%s,\"ageMs\":",
            boolean(s.lightValid), boolean(s.lightValid && s.lightAvailable && !s.lightError && lightAge <= 1500));
        if (s.lightValid) json.add("%lu,\"proximity\":%u,\"als0\":%u,\"als1\":%u",
            static_cast<unsigned long>(lightAge), s.proximity, s.light0, s.light1);
        else json.add("null,\"proximity\":null,\"als0\":null,\"als1\":null");
        json.add("},\"imu\":{\"available\":%s,\"error\":", boolean(s.imuAvailable));
        error(s.imuError);
        const auto vector = [&](const char* name, const ImuVector& v) {
            const uint32_t age = now - v.at;
            json.add(",\"%s\":{\"hasSample\":%s,\"fresh\":%s,\"ageMs\":", name,
                boolean(v.valid), boolean(v.valid && s.imuAvailable && !s.imuError && age <= 1000));
            if (v.valid) json.add("%lu,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f",
                static_cast<unsigned long>(age), static_cast<double>(v.x), static_cast<double>(v.y),
                static_cast<double>(v.z));
            else json.add("null,\"x\":null,\"y\":null,\"z\":null");
            json.add("}");
        };
        vector("accelerationG", s.acceleration);
        vector("gyroscopeDps", s.gyroscope);
        json.add("},\"rgb\":{\"active\":%s,\"durationMs\":", boolean(s.rgbActive));
        if (s.rgbActive) json.add("%lu", static_cast<unsigned long>(s.rgbDurationMs));
        else json.add("null");
        json.add(",\"error\":"); error(s.rgbError);
        json.add("},\"petting\":{\"enabled\":%s,\"soundEnabled\":%s,\"quiet\":%s,"
            "\"eligible\":%s,\"ineligibleReason\":", boolean(s.pettingEnabled), boolean(s.soundEnabled),
            boolean(s.quiet), boolean(s.eligible));
        error(s.ineligibleReason);
        json.add(",\"settled\":%s,\"released\":%s,\"touchCount\":%u,\"releaseCount\":%u,"
            "\"active\":%s,\"stage\":%u,\"soundState\":\"%s\"}}",
            boolean(s.settled), boolean(s.released), s.touchCount, s.releaseCount,
            boolean(s.pettingActive), s.stage, s.soundState);
        if (!json.ok) return reply(request, 503, "{\"error\":\"sensor_encoding_failed\"}");
        return reply(request, 200, uiJson);
    }
    char uiJson[2048];
    char debugJson[14336];
    RemoteLogPage logPage;
    OperationalEventPage operationalEventPage;
    // The single HTTP server task owns response scratch, not its 8 KiB stack.
    MotionDebugSnapshot motionDebugDetail;
    char retainedStatusJson[1536];
    char requestBody[kMaximumConfigBodyBytes + 1];
    ConfigurationRequest configRequest;
    MotionConfigurationSnapshot configSnapshot;
    char configJson[4096];

    static void configurationValueJson(Json& json, const MotionConfiguration& value) {
        json.add("{\"expectedYawRaw\":%d,\"expectedPitchRaw\":%d,\"homeYawRaw\":%d,\"homePitchRaw\":%d,"
            "\"yawMinRaw\":%d,\"yawMaxRaw\":%d,\"pitchMinRaw\":%d,\"pitchMaxRaw\":%d,"
            "\"stepRaw\":%d,\"nodStepRaw\":%d,\"travelMs\":%lu}",
            value.expectedYawRaw, value.expectedPitchRaw, value.homeYawRaw, value.homePitchRaw,
            value.yawMinRaw, value.yawMaxRaw, value.pitchMinRaw, value.pitchMaxRaw,
            value.stepRaw, value.nodStepRaw, static_cast<unsigned long>(value.travelMs));
    }
    const char* configurationBusy() const {
        if (device.stopRequested) return "stopping";
        if (device.manualActive()) return "manual_session_active";
        if (device.ui.kind != UiRequest::Kind::None || device.uiInProgress) return "ui_busy";
        return nullptr;
    }
    esp_err_t motionConfigurationRequest(httpd_req_t* request, const char* body = nullptr, size_t length = 0) {
        if (body && !parseConfiguration(body, length, configRequest))
            return reply(request, 400, "{\"error\":\"invalid_motion_config\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        auto& current = configSnapshot;
        device.runtime.configurationSnapshot(current);
        if (!current.supported) {
            lock.unlock();
            return reply(request, 422, "{\"error\":\"config_unavailable\"}");
        }
        const char* conflict = body ?
            device.runtime.configurationConflict(configRequest.bootId, configRequest.baseRevision) : nullptr;
        if (conflict) {
            Json json(configJson, sizeof(configJson));
            json.add("{\"error\":\"%s\"}", conflict);
            lock.unlock();
            return reply(request, 409, configJson);
        }
        if (body && configRequest.operation == ConfigurationRequest::Operation::ManualAction) {
            const char* error = device.queueManualAction(configRequest.bootId, configRequest.commandId,
                configRequest.manualAction, configRequest.manualDelta, configRequest.manualSupported,
                configRequest.singleAxis.expectedCalibration);
            Json json(configJson, sizeof(configJson));
            if (error) json.add("{\"error\":\"%s\"}", error);
            else json.add("{\"accepted\":true,\"bootId\":%lu,\"commandId\":%lu,"
                "\"outcome\":\"pending\",\"motionAvailable\":false}",
                static_cast<unsigned long>(configRequest.bootId),
                static_cast<unsigned long>(configRequest.commandId));
            lock.unlock();
            return reply(request, error ? 409 : 202, configJson);
        }
        if (body && configRequest.operation == ConfigurationRequest::Operation::PrepareSingleAxis) {
            configRequest.singleAxis.maximumFeedbackAgeMs = current.bounds.freshMs;
            const char* error = device.manualActive() ?
                (device.canQueueManualPreparation(configRequest.singleAxis.axis) ? nullptr : "manual_session_active") :
                configurationBusy();
            if (!error && (device.ui.kind != UiRequest::Kind::None || device.uiInProgress)) error = "ui_busy";
            if (!error && !device.acceptsManualTrial(configRequest.singleAxis)) error = "manual_local_trial_invalid";
            if (!error) error = device.runtime.prepareSingleAxis(configRequest.bootId, configRequest.baseRevision,
                configRequest.singleAxis, configRequest.singleAxisTravelMs);
            if (!error && device.manualAllowed() && !device.queueManualPreparation(configRequest.bootId,
                    configRequest.singleAxis, configRequest.singleAxisTravelMs)) {
                device.runtime.invalidateSingleAxisPreparation();
                error = "manual_session_unavailable";
            }
            configRequest = ConfigurationRequest{};
            if (error) {
                Json json(configJson, sizeof(configJson));
                json.add("{\"error\":\"%s\"}", error);
                lock.unlock();
                return reply(request, 409, configJson);
            }
            Json json(configJson, sizeof(configJson));
            json.add("{\"configurationStored\":true,\"motionAvailable\":false,"
                "\"reason\":\"manual_activation_not_validated\","
                "\"revision\":%lu,\"physicalReview\":\"operator_declared\"}",
                static_cast<unsigned long>(device.runtime.snapshot().configRevision));
            lock.unlock();
            return reply(request, 200, configJson);
        }
        if (body && configRequest.operation == ConfigurationRequest::Operation::Observe) {
            const char* admission = configurationBusy();
            if (!admission) admission = device.runtime.requestMotionObservation(
                configRequest.bootId, configRequest.observationId, millis());
            if (admission) {
                Json json(configJson, sizeof(configJson));
                json.add("{\"error\":\"%s\"}", admission);
                lock.unlock();
                return reply(request, 409, configJson);
            }
            device.runtime.debugSnapshot(motionDebugDetail);
            const auto& observation = motionDebugDetail.hardware;
            Json json(configJson, sizeof(configJson));
            json.add("{\"schemaVersion\":1,\"bootId\":%lu,\"revision\":%lu,\"readOnly\":true,"
                "\"physicalValidation\":\"unverified\",\"powerEffect\":\"not_requested\","
                "\"calibration\":",
                static_cast<unsigned long>(device.runtime.snapshot().bootId),
                static_cast<unsigned long>(current.revision));
            calibrationJson(json, motionDebugDetail.calibration);
            json.add(",\"hardwareObservation\":");
            hardwareObservationJson(json, observation);
            json.add("}");
            if (!json.ok) {
                lock.unlock();
                return reply(request, 503, "{\"error\":\"observation_encoding_failed\"}");
            }
            const int code = !strcmp(observation.state, "pending") ? 202 : 200;
            lock.unlock();
            return reply(request, code, configJson);
        }
        const bool reset = body && configRequest.operation == ConfigurationRequest::Operation::Reset;
        const bool preview = body && configRequest.operation == ConfigurationRequest::Operation::Preview;
        if (reset) configRequest.value = current.compiled;
        const auto& candidate = body ? configRequest.value : current.active;
        const char* validation = body ? device.runtime.validateConfiguration(candidate) :
            !current.configured ? current.applyReason : nullptr;
        const char* admission = configurationBusy();
        if (!admission) admission = body ? device.runtime.canApplyConfiguration(candidate) : current.applyReason;
        const bool commit = body && !preview && !validation && !admission;
        const int code = body && !preview ? validation ? 422 : admission ? 409 : 200 : 200;
        device.runtime.debugSnapshot(motionDebugDetail);
        const auto& observation = motionDebugDetail.calibration;
        if (commit) {
            motionDebugDetail.calibration.referenceConfigured = true;
            motionDebugDetail.calibration.expectedYaw = candidate.expectedYawRaw;
            motionDebugDetail.calibration.expectedPitch = candidate.expectedPitchRaw;
            motionDebugDetail.calibration.homeYaw = candidate.homeYawRaw;
            motionDebugDetail.calibration.homePitch = candidate.homePitchRaw;
        }
        const auto boot = device.runtime.snapshot().bootId;
        Json json(configJson, sizeof(configJson));
        json.add("{\"schemaVersion\":1,\"bootId\":%lu,\"revision\":%lu,\"source\":\"%s\",\"units\":\"scs_raw\","
            "\"hardwareValidated\":false,",
            static_cast<unsigned long>(boot), static_cast<unsigned long>(current.revision + (commit ? 1 : 0)),
            commit ? reset ? "compiled" : "operator_ram" :
            !current.configured ? "unconfigured" : current.operatorSelected ? "operator_ram" : "compiled");
        if (device.runtime.snapshot().motion.checkedDefault)
            json.add("\"configured\":%s,", commit || current.configured ? "true" : "false");
        json.add("\"busStarted\":%s,\"active\":", current.busStarted ? "true" : "false");
        if (commit || current.configured) configurationValueJson(json, commit ? candidate : current.active);
        else json.add("null");
        json.add(",\"compiled\":");
        if (current.configured) configurationValueJson(json, current.compiled);
        else json.add("null");
        json.add(",\"bounds\":{\"toleranceRaw\":%d,\"travelMinMs\":%lu,\"travelMaxMs\":%lu},"
            "\"valid\":%s,\"error\":", current.bounds.tolerance,
            static_cast<unsigned long>(current.bounds.travelMs),
            static_cast<unsigned long>((current.bounds.timeoutMs - 1) / 2 - current.bounds.settleMs),
            validation ? "false" : "true");
        if (validation || code != 200) json.add("\"%s\"", validation ? validation : admission);
        else json.add("null");
        json.add(",\"applyAllowed\":%s,\"applyReason\":", admission ? "false" : "true");
        if (admission) json.add("\"%s\"", admission); else json.add("null");
        json.add(",\"candidate\":");
        if (body) configurationValueJson(json, candidate); else json.add("null");
        json.add(",\"calibrationMatches\":");
        if ((!body && !current.configured) || !observation.yaw.valid() || !observation.pitch.valid()) json.add("null");
        else json.add("%s", observation.yaw.raw == candidate.expectedYawRaw &&
            observation.pitch.raw == candidate.expectedPitchRaw ? "true" : "false");
        json.add(",\"observationAgeMs\":");
        if (!strcmp(observation.phase, "not_read")) json.add("null");
        else json.add("%lu", static_cast<unsigned long>(millis() - observation.atMs));
        json.add(",\"calibration\":");
        calibrationJson(json, observation);
        json.add(",\"hardwareObservation\":");
        hardwareObservationJson(json, motionDebugDetail.hardware);
        json.add(",\"torqueRelease\":");
        torqueReleaseJson(json, motionDebugDetail.torqueRelease);
        const auto& stop = motionDebugDetail.emergencyStop;
        json.add(",\"emergencyStop\":{\"requested\":%s,\"uartOwned\":%s,"
            "\"txAttempted\":%s,\"txDrained\":%s,\"txUncertain\":%s,"
            "\"railAttempted\":%s,\"railRequestAccepted\":%s}",
            stop.requested ? "true" : "false", stop.uartOwned ? "true" : "false",
            stop.txAttempted ? "true" : "false", stop.txDrained ? "true" : "false",
            stop.txUncertain ? "true" : "false", stop.railAttempted ? "true" : "false",
            stop.railRequestAccepted ? "true" : "false");
        if (device.manualActive()) {
            const bool currentSession = device.manualSessionCurrent();
            const auto receipt = device.manualActionReceipt();
            json.add(",\"manualSession\":{\"state\":\"%s\",\"bootId\":%lu,\"revision\":%lu,"
                "\"lastCommandId\":%lu,\"command\":",
                currentSession ? manualSessionStateName(device.manualSnapshot()) : "invalidated",
                static_cast<unsigned long>(boot),
                static_cast<unsigned long>(currentSession ? device.manualSessionRevision() : 0),
                static_cast<unsigned long>(device.manualLastCommandId()));
            if (receipt.commandId)
                json.add("{\"bootId\":%lu,\"revision\":%lu,\"commandId\":%lu,"
                    "\"action\":\"%s\",\"outcome\":\"%s\"}",
                    static_cast<unsigned long>(receipt.bootId),
                    static_cast<unsigned long>(receipt.revision),
                    static_cast<unsigned long>(receipt.commandId), receipt.action, receipt.outcome);
            else json.add("null");
            json.add(",\"error\":");
            if (!currentSession) json.add("\"manual_boot_changed\"");
            else if (device.manualError()) json.add("\"%s\"", device.manualError());
            else json.add("null");
            const auto feedback = device.manualFeedbackSnapshot();
            json.add(",\"feedback\":{\"supported\":true,\"stationary\":%s,\"feedbackOnly\":%s,"
                "\"yawRaw\":", currentSession && feedback.stationary ? "true" : "false",
                currentSession && feedback.feedbackOnly ? "true" : "false");
            if (currentSession && feedback.stationary) json.add("%d", feedback.position.yaw);
            else json.add("null");
            json.add(",\"pitchRaw\":");
            if (currentSession && feedback.stationary) json.add("%d", feedback.position.pitch);
            else json.add("null");
            json.add("}");
            json.add(",\"motionAvailable\":false}");
        }
        json.add("}");
        if (!json.ok) {
            lock.unlock();
            return reply(request, 503, "{\"error\":\"config_encoding_failed\"}");
        }
        // Encode the complete prospective receipt before the non-failing RAM transaction.
        if (commit && !device.runtime.applyConfiguration(candidate, reset)) {
            lock.unlock();
            return reply(request, 409, "{\"error\":\"config_rejected_refresh\"}");
        }
        lock.unlock();
        return reply(request, code, configJson);
    }

    esp_err_t logs(httpd_req_t* request) {
        if (!logsEnabled) return reply(request, 409, "{\"error\":\"debug_disabled\"}");
        RemoteLogQuery query;
        if (!parseRemoteLogQuery(request->uri, query))
            return reply(request, 400, "{\"error\":\"invalid_log_query\"}");
        const char* error = remoteLog->read(query, logPage);
        Json json(debugJson, sizeof(debugJson));
        if (error) {
            json.add("{\"error\":\"%s\",\"bootId\":%lu,\"oldestSeq\":%lu,\"latestSeq\":%lu}", error,
                static_cast<unsigned long>(logPage.bootId), static_cast<unsigned long>(logPage.oldestSeq),
                static_cast<unsigned long>(logPage.latestSeq));
            return reply(request, !strcmp(error, "log_unavailable") || !strcmp(error, "log_sequence_exhausted") ? 503 : 409, debugJson);
        }
        json.add("{\"schemaVersion\":1,\"snapshotKind\":\"retained\",\"firmwareVersion\":\"%s\","
            "\"bootId\":%lu,\"capacity\":%u,\"oldestSeq\":%lu,\"latestSeq\":%lu,\"nextAfter\":%lu,"
            "\"hasMore\":%s,\"overwritten\":%lu,\"dropped\":%lu,\"queueLost\":%lu,\"traceSkipped\":%lu,\"entries\":[",
            NETBOT_FIRMWARE_VERSION, static_cast<unsigned long>(logPage.bootId), static_cast<unsigned>(kRemoteLogCapacity),
            static_cast<unsigned long>(logPage.oldestSeq), static_cast<unsigned long>(logPage.latestSeq),
            static_cast<unsigned long>(logPage.nextAfter), logPage.hasMore ? "true" : "false",
            static_cast<unsigned long>(logPage.overwritten), static_cast<unsigned long>(logPage.dropped),
            static_cast<unsigned long>(logPage.queueLost), static_cast<unsigned long>(logPage.traceSkipped));
        for (uint8_t i = 0; i < logPage.count; ++i) {
            const auto& entry = logPage.entries[i];
            json.add("%s{\"seq\":%lu,\"line\":\"", i ? "," : "", static_cast<unsigned long>(entry.seq));
            for (const char* ch = entry.line; *ch; ++ch) {
                if (*ch == '"' || *ch == '\\') json.add("\\");
                json.add("%c", *ch);
            }
            json.add("\"}");
        }
        json.add("]}");
        return json.ok ? reply(request, 200, debugJson) :
            reply(request, 503, "{\"error\":\"log_encoding_failed\"}");
    }
    esp_err_t events(httpd_req_t* request) {
        char schema[2]{};
        const auto header = httpd_req_get_hdr_value_str(request, "X-NetBot-Events-Schema", schema, sizeof(schema));
        if (header != ESP_ERR_NOT_FOUND &&
            (header != ESP_OK || (strcmp(schema, "1") && strcmp(schema, "2"))))
            return reply(request, 400, "{\"error\":\"invalid_event_schema\"}");
        const bool v2 = header == ESP_OK && schema[0] == '2';
        OperationalEventQuery query;
        if (!parseOperationalEventQuery(request->uri, query))
            return reply(request, 400, "{\"error\":\"invalid_event_query\"}");
        const char* error = operationalEvents.read(query, operationalEventPage);
        Json json(debugJson, sizeof(debugJson));
        if (error) {
            const bool cursorError = !strcmp(error, "boot_changed") ||
                !strcmp(error, "event_cursor_expired") || !strcmp(error, "event_cursor_ahead");
            if (cursorError) {
                json.add("{\"error\":\"%s\",\"bootId\":%lu,\"oldestSeq\":%lu,\"latestSeq\":%lu}",
                    error, static_cast<unsigned long>(operationalEventPage.bootId),
                    static_cast<unsigned long>(operationalEventPage.oldestSeq),
                    static_cast<unsigned long>(operationalEventPage.latestSeq));
                return reply(request, 409, debugJson);
            }
            json.add("{\"error\":\"%s\"}", error);
            return reply(request, 503, debugJson);
        }
        json.add("{\"schemaVersion\":%u,\"snapshotKind\":\"operational\",\"bootId\":%lu,"
            "\"capacity\":%u,\"oldestSeq\":%lu,\"latestSeq\":%lu,\"nextAfter\":%lu,"
            "\"hasMore\":%s,\"overwritten\":%lu,\"dropped\":%lu,\"entries\":[",
            v2 ? 2u : 1u, static_cast<unsigned long>(operationalEventPage.bootId),
            static_cast<unsigned>(kOperationalEventCapacity),
            static_cast<unsigned long>(operationalEventPage.oldestSeq),
            static_cast<unsigned long>(operationalEventPage.latestSeq),
            static_cast<unsigned long>(operationalEventPage.nextAfter),
            operationalEventPage.hasMore ? "true" : "false",
            static_cast<unsigned long>(operationalEventPage.overwritten),
            static_cast<unsigned long>(operationalEventPage.dropped));
        for (uint8_t i = 0; i < operationalEventPage.count; ++i) {
            const auto& entry = operationalEventPage.entries[i];
            json.add("%s{\"seq\":%lu,\"atMs\":%lu,\"event\":\"%s\",\"status\":\"%s\","
                "\"action\":\"%s\",\"error\":\"%s\",\"correlationId\":%lu,"
                "\"relatedId\":%lu,\"revision\":%lu",
                i ? "," : "", static_cast<unsigned long>(entry.seq), static_cast<unsigned long>(entry.atMs),
                operationalEventName(entry.event), operationalStatusName(entry.status),
                operationalActionName(entry.action), operationalErrorName(entry.error),
                static_cast<unsigned long>(entry.correlationId), static_cast<unsigned long>(entry.relatedId),
                static_cast<unsigned long>(entry.revision));
            if (v2) {
                json.add(",\"interactionOrigin\":\"%s\",\"brightnessRequested\":",
                    interactionOriginName(entry.metadata.origin));
                if (entry.metadata.valid & OperationalMetadata::Requested) json.add("%u", entry.metadata.requested);
                else json.add("null");
                json.add(",\"brightnessBefore\":");
                if (entry.metadata.valid & OperationalMetadata::Before) json.add("%u", entry.metadata.before);
                else json.add("null");
                json.add(",\"brightnessAfter\":");
                if (entry.metadata.valid & OperationalMetadata::After) json.add("%u", entry.metadata.after);
                else json.add("null");
            }
            json.add("}");
        }
        json.add("]}");
        return json.ok ? reply(request, 200, debugJson) :
            reply(request, 503, "{\"error\":\"event_encoding_failed\"}");
    }

    static void observationJson(Json& json, const BusObservation& value) {
        json.add("{\"sequence\":%lu,\"commandId\":%lu,\"atMs\":%lu,\"stage\":\"%s\",\"operation\":\"%s\","
            "\"servoId\":%u,\"register\":%u,\"width\":%u,\"value\":%d,\"transportError\":%d,"
            "\"deviceStatus\":%d,\"ok\":%s}",
            static_cast<unsigned long>(value.sequence), static_cast<unsigned long>(value.commandId),
            static_cast<unsigned long>(value.atMs), value.stage, value.operation,
            static_cast<unsigned>(value.servoId), static_cast<unsigned>(value.reg),
            static_cast<unsigned>(value.width), value.value, value.transportError,
            value.deviceStatus, value.ok ? "true" : "false");
    }
    static void calibrationAxisJson(Json& json, const CalibrationAxisObservation& axis,
                                   const char* key, uint8_t servoId, const int* expected) {
        json.add("{\"servoId\":%u,\"key\":\"%s\",\"readResult\":\"%s\",\"readCode\":",
            static_cast<unsigned>(servoId), key, calibrationReadName(axis.result));
        if (axis.result == CalibrationReadResult::NotRead) json.add("null");
        else json.add("%ld", static_cast<long>(axis.code));
        json.add(",\"raw\":");
        if (axis.hasValue()) json.add("%ld", static_cast<long>(axis.raw));
        else json.add("null");
        json.add(",\"rangeValid\":%s,\"expected\":", axis.valid() ? "true" : "false");
        if (expected) json.add("%d", *expected); else json.add("null");
        json.add(",\"comparison\":\"%s\"}",
            !axis.valid() || !expected ? "not_comparable" :
            axis.raw == *expected ? "match" : "mismatch");
    }
    static void calibrationJson(Json& json, const CalibrationObservation& observation) {
        json.add("{\"referenceConfigured\":%s,\"phase\":\"%s\",\"atMs\":",
            observation.referenceConfigured ? "true" : "false", observation.phase);
        if (!strcmp(observation.phase, "not_read")) json.add("null,\"commandId\":null");
        else json.add("%lu,\"commandId\":%lu", static_cast<unsigned long>(observation.atMs),
            static_cast<unsigned long>(observation.commandId));
        json.add(",\"namespace\":\"servo\",\"encoding\":\"int32\",\"units\":\"raw_servo_zero\","
            "\"origin\":\"unknown\",\"physicalValidation\":\"unverified\","
            "\"open\":{\"result\":\"%s\",\"code\":",
            calibrationReadName(observation.openResult));
        if (observation.openResult == CalibrationReadResult::NotRead) json.add("null");
        else json.add("%ld", static_cast<long>(observation.openCode));
        json.add("},\"yaw\":");
        calibrationAxisJson(json, observation.yaw, "zero_pos_1", 1,
            observation.referenceConfigured ? &observation.expectedYaw : nullptr);
        json.add(",\"pitch\":");
        calibrationAxisJson(json, observation.pitch, "zero_pos_2", 2,
            observation.referenceConfigured ? &observation.expectedPitch : nullptr);
        const unsigned mismatch = (observation.yaw.valid() && observation.yaw.raw != observation.expectedYaw ? 1U : 0U) |
            (observation.pitch.valid() && observation.pitch.raw != observation.expectedPitch ? 2U : 0U);
        json.add(",\"home\":");
        if (observation.referenceConfigured)
            json.add("{\"yaw\":%d,\"pitch\":%d}", observation.homeYaw, observation.homePitch);
        else json.add("null");
        json.add(",\"mismatchMask\":");
        if (observation.referenceConfigured) json.add("%u}", mismatch);
        else json.add("null}");
    }
    static void motionValueJson(Json& json, const MotionValueObservation& value) {
        json.add("{\"attempted\":%s,\"value\":", value.attempted ? "true" : "false");
        if (value.attempted && value.value >= 0) json.add("%d", value.value);
        else json.add("null");
        json.add(",\"transportError\":");
        if (value.attempted) json.add("%d", value.transportError); else json.add("null");
        json.add(",\"deviceStatus\":");
        if (value.attempted) json.add("%d", value.deviceStatus); else json.add("null");
        json.add(",\"transactionOk\":%s,\"valid\":%s}",
            value.attempted && value.transactionOk ? "true" : "false",
            value.attempted && value.valid ? "true" : "false");
    }
    static void hardwareObservationJson(Json& json, const HardwareMotionObservation& value) {
        json.add("{\"bootId\":%lu,\"observationId\":%lu,\"state\":\"%s\","
            "\"requestedAtMs\":%lu,\"finishedAtMs\":",
            static_cast<unsigned long>(value.bootId), static_cast<unsigned long>(value.observationId), value.state,
            static_cast<unsigned long>(value.requestedAtMs));
        if (!strcmp(value.state, "idle") || !strcmp(value.state, "pending")) json.add("null");
        else json.add("%lu", static_cast<unsigned long>(value.finishedAtMs));
        json.add(",\"positionUnits\":\"scs_raw\",\"uartInitialized\":%s,\"powerEffect\":\"not_requested\","
            "\"servoRail\":\"unobserved\",\"error\":",
            value.busInitialized ? "true" : "false");
        if (value.error) json.add("\"%s\"", value.error); else json.add("null");
        json.add(",\"yaw\":{\"position\":");
        motionValueJson(json, value.yaw.position);
        json.add(",\"moving\":");
        motionValueJson(json, value.yaw.moving);
        json.add(",\"torque\":");
        motionValueJson(json, value.yaw.torque);
        json.add("},\"pitch\":{\"position\":");
        motionValueJson(json, value.pitch.position);
        json.add(",\"moving\":");
        motionValueJson(json, value.pitch.moving);
        json.add(",\"torque\":");
        motionValueJson(json, value.pitch.torque);
        json.add("}}");
    }
    static void torqueReleaseAxisJson(Json& json, const TorqueReleaseAxisObservation& value) {
        json.add("{\"before\":");
        if (value.before >= 0) json.add("%d", value.before); else json.add("null");
        json.add(",\"writeAttempted\":%s,\"write\":", value.writeAttempted ? "true" : "false");
        if (value.writeAttempted) json.add("%d", value.write); else json.add("null");
        json.add(",\"writeError\":");
        if (value.writeAttempted) json.add("%d", value.writeError); else json.add("null");
        json.add(",\"writeStatus\":");
        if (value.writeAttempted) json.add("%d", value.writeStatus); else json.add("null");
        json.add(",\"readback\":");
        if (value.writeAttempted && value.readback >= 0) json.add("%d", value.readback);
        else json.add("null");
        json.add(",\"readError\":");
        if (value.writeAttempted) json.add("%d", value.readError); else json.add("null");
        json.add(",\"readStatus\":");
        if (value.writeAttempted) json.add("%d", value.readStatus); else json.add("null");
        json.add(",\"finalTorque\":");
        if (value.finalTorque >= 0) json.add("%d}", value.finalTorque);
        else json.add("null}");
    }
    static void torqueReleaseJson(Json& json, const TorqueReleaseObservation& value) {
        json.add("{\"attempted\":%s,\"state\":\"%s\",\"commandId\":%lu,\"atMs\":",
            value.attempted ? "true" : "false", value.state, static_cast<unsigned long>(value.commandId));
        if (value.attempted) json.add("%lu", static_cast<unsigned long>(value.atMs));
        else json.add("null");
        json.add(",\"error\":");
        if (value.error) json.add("\"%s\"", value.error); else json.add("null");
        json.add(",\"writeUncertain\":%s,\"powerEffect\":\"not_requested\",\"servoRail\":\"unobserved\",\"yaw\":",
            value.writeUncertain ? "true" : "false");
        torqueReleaseAxisJson(json, value.yaw);
        json.add(",\"pitch\":");
        torqueReleaseAxisJson(json, value.pitch);
        json.add("}");
    }
    static void enableAxisJson(Json& json, const EnableAxisObservation& a) {
        json.add("{\"goalAttempted\":%s,\"goalWrite\":%d,\"goalError\":%d,\"goalStatus\":%d,\"goalRead\":%d,"
            "\"position\":%d,\"moving\":%d,\"torque\":%d,\"torqueOnAfterGoal\":%s,\"enableWrite\":%d,"
            "\"cleanupAttempted\":%s,\"cleanupWrite\":%d,\"cleanupError\":%d,\"cleanupStatus\":%d,\"cleanupRead\":%d}",
            a.goalAttempted ? "true" : "false", a.goalWrite, a.goalError, a.goalStatus, a.goalRead,
            a.position, a.moving, a.torque, a.torqueOnAfterGoal ? "true" : "false", a.enableWrite,
            a.cleanupAttempted ? "true" : "false", a.cleanupWrite, a.cleanupError, a.cleanupStatus, a.cleanupRead);
    }
    // -1 means unread. Torque-off is not a mechanical stop or hold; the vendor cause of torque 1 is unverified.
    static void enableJson(Json& json, const EnableObservation& value) {
        json.add("{\"attempted\":%s,\"cleanup\":\"%s\",\"cause\":\"unverified\",\"yaw\":",
            value.attempted ? "true" : "false", enableCleanupName(value.cleanup));
        enableAxisJson(json, value.yaw);
        json.add(",\"pitch\":");
        enableAxisJson(json, value.pitch);
        json.add("}");
    }
    esp_err_t motionDebug(httpd_req_t* request, int code = 200) {
        Snapshot status;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            status = device.runtime.snapshot();
            if (!status.motion.remoteYawBench && !status.motion.twoAxisTest)
                return reply(request, 409, "{\"error\":\"debug_disabled\"}");
            device.runtime.debugSnapshot(motionDebugDetail);
        }
        return motionDebugReply(request, code, status);
    }
    esp_err_t motionDebugReply(httpd_req_t* request, int code, const Snapshot& status) {
        const auto& detail = motionDebugDetail;
        if (!statusJson(status, retainedStatusJson, sizeof(retainedStatusJson)))
            return reply(request, 503, "{\"error\":\"debug_encoding_failed\"}");
        Json json(debugJson, sizeof(debugJson));
        json.add("{\"snapshotKind\":\"retained\",\"status\":%s,\"busStarted\":%s,"
            "\"traceFrozen\":%s,\"traceOverwritten\":%lu,\"trace\":[",
            retainedStatusJson, detail.busStarted ? "true" : "false", detail.frozen ? "true" : "false",
            static_cast<unsigned long>(detail.overwritten));
        for (uint8_t i = 0; i < detail.count; ++i) {
            if (i) json.add(",");
            observationJson(json, detail.trace[i]);
        }
        const auto& sample = detail.sample;
        json.add("],\"sample\":{\"sampleId\":%lu,\"servoId\":%u,\"state\":\"%s\","
            "\"requestedAtMs\":%lu,\"finishedAtMs\":%lu,\"error\":",
            static_cast<unsigned long>(sample.sampleId), static_cast<unsigned>(sample.servoId), sample.state,
            static_cast<unsigned long>(sample.requestedAtMs), static_cast<unsigned long>(sample.finishedAtMs));
        if (sample.error) json.add("\"%s\"", sample.error);
        else json.add("null");
        json.add(",\"values\":[");
        for (uint8_t i = 0; i < sample.count; ++i) {
            if (i) json.add(",");
            observationJson(json, sample.values[i]);
        }
        json.add("]},\"calibration\":");
        calibrationJson(json, detail.calibration);
        json.add(",\"enable\":");
        enableJson(json, detail.enable);
        json.add("}");
        return json.ok ? reply(request, code, debugJson) :
            reply(request, 503, "{\"error\":\"debug_encoding_failed\"}");
    }
    esp_err_t servoSample(httpd_req_t* request, const char* body, size_t length) {
        if (!flatJson(body, length)) return reply(request, 400, "{\"error\":\"invalid_sample\"}");
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        uint32_t boot = 0, id = 0, servo = 0;
        if (root) while (end < body + length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
        const bool valid = root && end == body + length && cJSON_IsObject(root) &&
            cJSON_GetArraySize(root) == 3 && unsignedField(root, "bootId", boot) &&
            unsignedField(root, "sampleId", id) && unsignedField(root, "servoId", servo) && servo <= 2;
        cJSON_Delete(root);
        if (!valid) return reply(request, 400, "{\"error\":\"invalid_sample\"}");
        const char* error;
        bool pending;
        Snapshot status;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            error = device.stopRequested ? "stopping" : device.manualActive() ? "manual_session_active" :
                device.runtime.requestSample(boot, id, servo, millis());
            device.runtime.debugSnapshot(motionDebugDetail);
            pending = !strcmp(motionDebugDetail.sample.state, "pending");
            status = device.runtime.snapshot();
        }
        if (error) {
            char json[96];
            snprintf(json, sizeof(json), "{\"error\":\"%s\"}", error);
            return reply(request, 409, json);
        }
        if (!status.motion.remoteYawBench && !status.motion.twoAxisTest)
            return reply(request, 409, "{\"error\":\"debug_disabled\"}");
        return motionDebugReply(request, pending ? 202 : 200, status);
    }

    esp_err_t uiScreens(httpd_req_t* request) {
        if (!remoteUi) return reply(request, 409, "{\"error\":\"remote_ui_disabled\"}");
        Page current;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            current = device.uiPage;
        }
        Json json(uiJson, sizeof(uiJson));
        json.add("{\"current\":\"%s\",\"screens\":[", findScreen(current)->id);
        bool first = true;
        for (const auto& screen : screens) {
            json.add("%s{\"id\":\"%s\",\"title\":\"%s\"}", first ? "" : ",", screen.id, screen.title);
            first = false;
        }
        json.add("]}");
        return json.ok ? reply(request, 200, uiJson) : reply(request, 503, "{\"error\":\"ui_encoding_failed\"}");
    }
    static const char* elementType(const Button& button) {
        switch (button.choice) {
            case MenuChoice::Back: case MenuChoice::Demo: case MenuChoice::Motion:
            case MenuChoice::Network: case MenuChoice::Settings: case MenuChoice::Shutdown:
            case MenuChoice::Sensors: case MenuChoice::Nfc: case MenuChoice::Navigate: return "navigate";
            case MenuChoice::Action: case MenuChoice::AvatarSampleA: case MenuChoice::AvatarSampleB:
            case MenuChoice::AvatarSampleC: case MenuChoice::AvatarSampleD: return "action";
            case MenuChoice::AvatarTest: return "navigate";
            case MenuChoice::NotificationAction: case MenuChoice::NotificationDiscard:
            case MenuChoice::NotificationAcknowledge: return "action";
            case MenuChoice::NotificationOpen: return "navigate";
            case MenuChoice::Stop: return "stop";
            case MenuChoice::Quiet: case MenuChoice::StatusStrip:
            case MenuChoice::Petting: case MenuChoice::PettingSound: return "toggle";
            case MenuChoice::BrightnessDown: case MenuChoice::BrightnessUp: return "adjust";
            default: return "system";
        }
    }
    const char* brightnessControlError(const Button& button, DisplayBrightnessStatus& state,
                                       bool* contended = nullptr) {
        if (button.choice != MenuChoice::BrightnessDown && button.choice != MenuChoice::BrightnessUp)
            return nullptr;
        if (!displayBrightnessStatus(state, true, contended))
            return contended && *contended ? "device_busy" : "brightness_unavailable";
        if (state.pending) return "brightness_busy";
        if (button.choice == MenuChoice::BrightnessDown &&
            state.brightness <= kDisplayBrightnessMinimum) return "brightness_at_minimum";
        if (button.choice == MenuChoice::BrightnessUp &&
            state.brightness >= kDisplayBrightnessMaximum) return "brightness_at_maximum";
        return nullptr;
    }
    esp_err_t uiScreen(httpd_req_t* request) {
        if (!remoteUi) return reply(request, 409, "{\"error\":\"remote_ui_disabled\"}");
        Page current;
        NotificationIdentity rendered;
        bool faceStopVisible, pettingEnabled, pettingSoundEnabled;
        {
            auto lock = httpLock(device.mutex);
            if (!lock.owns_lock()) return busy(request);
            current = device.uiPage;
            rendered = device.uiNotification;
            faceStopVisible = device.uiFaceStopVisible;
            pettingEnabled = device.uiPettingEnabled;
            pettingSoundEnabled = device.uiPettingSoundEnabled;
        }
        const Screen* screen = findScreen(current);
        Json json(uiJson, sizeof(uiJson));
        json.add("{\"id\":\"%s\",\"title\":\"%s\",\"elements\":[", screen->id, screen->title);
        size_t count;
        const Button* items = pageButtons(current, count);
        bool first = true;
        for (size_t i = 0; i < count; ++i) {
            if (!visibleButton(current, faceStopVisible)) continue;
            char id[40];
            if (!buttonId(items[i], id, sizeof(id))) return reply(request, 503, "{\"error\":\"ui_encoding_failed\"}");
            DisplayBrightnessStatus brightness;
            bool brightnessContended = false;
            const char* controlError = brightnessControlError(items[i], brightness, &brightnessContended);
            if (brightnessContended) return busy(request);
            json.add("%s{\"id\":\"%s\",\"label\":\"%s\",\"type\":\"%s\",\"enabled\":%s,\"motion\":%s,"
                     "\"bounds\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}}",
                     first ? "" : ",", id, items[i].label, elementType(items[i]),
                     remoteBlocked(items[i]) || controlError ? "false" : "true",
                     isMotionElement(items[i]) ? "true" : "false",
                     items[i].x, items[i].y, items[i].w, items[i].h);
            first = false;
        }
        json.add("]");
        if (current == Page::Petting)
            json.add(",\"petting\":{\"enabled\":%s,\"soundEnabled\":%s}",
                pettingEnabled ? "true" : "false", pettingSoundEnabled ? "true" : "false");
        if (current == Page::AvatarTest) {
            AvatarComparisonStatus comparison;
            {
                auto lock = httpLock(avatarComparisonMutex);
                if (!lock.owns_lock()) return busy(request);
                comparison = avatarComparison.snapshot(millis());
            }
            json.add(",\"avatarComparison\":{\"sample\":");
            if (comparison.selected) json.add("\"%s\"", avatarComparisonId(comparison.sample));
            else json.add("null");
            json.add(",\"active\":%s,\"remainingMs\":%lu}",
                comparison.active ? "true" : "false",
                static_cast<unsigned long>(comparison.remainingMs));
        }
        if (notificationPage(current))
            json.add(",\"bootId\":%lu,\"notificationId\":%lu,\"revision\":%lu",
                static_cast<unsigned long>(rendered.bootId),
                static_cast<unsigned long>(rendered.notificationId),
                static_cast<unsigned long>(rendered.revision));
        json.add("}");
        return json.ok ? reply(request, 200, uiJson) : reply(request, 503, "{\"error\":\"ui_encoding_failed\"}");
    }
    // Copies the string members of a flat JSON object with exactly the named keys.
    static bool uiStrings(const char* body, size_t length, const char* const* keys, char (*values)[40], size_t count) {
        if (!flatJson(body, length)) return false;
        const char* end = nullptr;
        cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
        bool valid = root && end == body + length && static_cast<size_t>(cJSON_GetArraySize(root)) == count;
        for (size_t i = 0; valid && i < count; ++i) {
            const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
            valid = cJSON_IsString(item) && strlen(item->valuestring) < 40;
            if (valid) strcpy(values[i], item->valuestring);
        }
        cJSON_Delete(root);
        return valid;
    }
    esp_err_t uiNavigate(httpd_req_t* request, const char* body, size_t length) {
        if (!remoteUi) return reply(request, 409, "{\"error\":\"remote_ui_disabled\"}");
        static const char* const keys[] = {"id"};
        char values[1][40];
        if (!uiStrings(body, length, keys, values, 1)) return reply(request, 400, "{\"error\":\"invalid_ui_request\"}");
        const Screen* screen = findScreen(values[0]);
        if (!screen) return reply(request, 422, "{\"error\":\"unknown_screen\"}");
        if (remoteScreenBlocked(screen->page))
            return reply(request, 409, "{\"error\":\"screen_blocked\"}");
        if (notificationPage(screen->page)) return reply(request, 409, "{\"error\":\"screen_blocked\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (device.notification.snapshot(millis()).active)
            return reply(request, 409, "{\"error\":\"notification_busy\"}");
        if (device.stopRequested) return reply(request, 409, "{\"error\":\"stopping\"}");
        if (device.ui.kind != UiRequest::Kind::None) return reply(request, 409, "{\"error\":\"ui_busy\"}");
        device.ui = {UiRequest::Kind::Screen, screen->page, 0};
        return reply(request, 202, "{\"accepted\":true}");
    }
    esp_err_t uiTouch(httpd_req_t* request, const char* body, size_t length) {
        if (!remoteUi) return reply(request, 409, "{\"error\":\"remote_ui_disabled\"}");
        static const char* const keys[] = {"screen", "element"};
        char values[2][40];
        NotificationIdentity identity;
        bool valid = uiStrings(body, length, keys, values, 2);
        if (!valid && flatJson(body, length)) {
            const char* end = nullptr;
            cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
            const char* const notificationKeys[] = {"screen", "element", "bootId", "notificationId", "revision"};
            const auto* screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
            const auto* element = cJSON_GetObjectItemCaseSensitive(root, "element");
            valid = cJSON_IsObject(root) && jsonEnd(end, body + length) &&
                exactKeys(root, notificationKeys, 5) && cJSON_IsString(screen) && cJSON_IsString(element) &&
                strlen(screen->valuestring) < 40 && strlen(element->valuestring) < 40 &&
                unsignedField(root, "bootId", identity.bootId) &&
                unsignedField(root, "notificationId", identity.notificationId) &&
                unsignedField(root, "revision", identity.revision);
            if (valid) {
                strcpy(values[0], screen->valuestring);
                strcpy(values[1], element->valuestring);
            }
            cJSON_Delete(root);
        }
        if (!valid) return reply(request, 400, "{\"error\":\"invalid_ui_request\"}");
        const Screen* screen = findScreen(values[0]);
        if (!screen) return reply(request, 422, "{\"error\":\"unknown_screen\"}");
        if (notificationPage(screen->page) != (identity.notificationId != 0))
            return reply(request, 400, "{\"error\":\"invalid_ui_request\"}");
        size_t count;
        const Button* items = pageButtons(screen->page, count);
        size_t index = count;
        for (size_t i = 0; i < count; ++i) {
            char id[40];
            if (buttonId(items[i], id, sizeof(id)) && !strcmp(id, values[1])) index = i;
        }
        if (index == count) return reply(request, 422, "{\"error\":\"unknown_element\"}");
        if (remoteBlocked(items[index]))
            return reply(request, 409, isMotionElement(items[index]) ? "{\"error\":\"motion_blocked\"}" :
                                                                      "{\"error\":\"element_blocked\"}");
        auto lock = httpLock(device.mutex);
        if (!lock.owns_lock()) return busy(request);
        if (!visibleButton(screen->page, device.uiFaceStopVisible))
            return reply(request, 409, "{\"error\":\"element_hidden\"}");
        device.notification.advance(millis(), companionPresent(millis()));
        if (notificationPage(screen->page) && !device.notification.matches(identity))
            return reply(request, 409, "{\"error\":\"stale_notification\"}");
        if (notificationPage(screen->page) &&
            (device.uiNotification.bootId != identity.bootId ||
             device.uiNotification.notificationId != identity.notificationId ||
             device.uiNotification.revision != identity.revision))
            return reply(request, 409, "{\"error\":\"stale_notification\"}");
        if (device.stopRequested) return reply(request, 409, "{\"error\":\"stopping\"}");
        if (device.uiPage != screen->page) return reply(request, 409, "{\"error\":\"wrong_screen\"}");
        if (device.ui.kind != UiRequest::Kind::None) return reply(request, 409, "{\"error\":\"ui_busy\"}");
        DisplayBrightnessStatus brightness;
        bool brightnessContended = false;
        const char* controlError = brightnessControlError(items[index], brightness, &brightnessContended);
        if (brightnessContended) return busy(request);
        if (controlError) {
            char json[64];
            snprintf(json, sizeof(json), "{\"error\":\"%s\"}", controlError);
            return reply(request, 409, json);
        }
        device.ui = {UiRequest::Kind::Touch, screen->page, static_cast<uint8_t>(index), identity};
        return reply(request, 202, "{\"accepted\":true}");
    }
};

}  // namespace lab
