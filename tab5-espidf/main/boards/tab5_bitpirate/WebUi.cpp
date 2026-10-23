#include "WebUi.h"

#include <string>
#include <vector>

#include <Arduino.h>
#include <WiFi.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "Tab5Gui.h"
#include "Tab5TerminalView.h"
#include "Services/LittleFsService.h"
#include "Servers/DnsServer.h"

#include "webui/index.h"
#include "webui/style.h"
#include "webui/scripts.h"

// esp_hosted's init already happens lazily inside WiFi.mode()/WiFi.softAP()
// (arduino-esp32's hostedInitWiFi()) -- do not call esp_hosted_init() here
// too, it races that and fails with "Transport already initialized".

namespace webui {

namespace {

const char* TAG = "webui";

bool s_apUp = false;
httpd_handle_t s_httpd = nullptr;

// Single-session model, matching the real web UI's own assumption ("One
// terminal session only") -- a second tab just takes over the fd.
int s_wsFd = -1;

// Set once the AP's IP is known; used by handleCaptivePortal() below.
std::string s_captivePortalUrl;

// Registered via Tab5TerminalView::setWebMirror() -- mirrors every
// print()/println() the touch GUI already renders to the browser too.
void sendToWsClient(const char* data, size_t len, bool withNewline) {
    if (s_wsFd < 0 || !s_httpd) return;
    if (len == 0 && !withNewline) return;

    std::string payload(data, len);
    if (withNewline) payload += '\n';

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t*)payload.data();
    frame.len = payload.size();

    esp_err_t err = httpd_ws_send_frame_async(s_httpd, s_wsFd, &frame);
    if (err != ESP_OK) {
        // Most common cause: the client already disconnected and we haven't
        // noticed yet -- drop the stale fd rather than keep failing silently
        // on every future print().
        ESP_LOGW(TAG, "ws send failed (%s), dropping client fd %d", esp_err_to_name(err), s_wsFd);
        s_wsFd = -1;
    }
}

// scripts.js sends one whole command + '\n' per frame, not per keystroke --
// queuing it byte by byte into the same queue the keyboard feeds is enough
// for CommandLineManager's existing char-by-char reader.
void feedCharQueue(const uint8_t* data, size_t len) {
    QueueHandle_t q = tab5gui::charQueue();
    if (!q) return;
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        xQueueSend(q, &c, 0);
    }
}

esp_err_t handleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}
esp_err_t handleStyle(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_send(req, style_css, HTTPD_RESP_USE_STRLEN);
}
esp_err_t handleScripts(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/javascript");
    return httpd_resp_send(req, scripts_js, HTTPD_RESP_USE_STRLEN);
}

// Catches a phone/laptop's OS connectivity-check probe and redirects it to
// the real page, instead of leaving the user to find the printed IP.
esp_err_t handleCaptivePortal(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!s_captivePortalUrl.empty()) {
        httpd_resp_set_hdr(req, "Location", s_captivePortalUrl.c_str());
        httpd_resp_set_status(req, "302 Found");
        return httpd_resp_send(req, nullptr, 0);
    }
    return handleIndex(req);
}

// /littlefs/* -- server half of scripts.js's existing Files-panel calls.
LittleFsService s_lfs;

bool ensureLfs() {
    if (s_lfs.mounted()) return true;
    return s_lfs.begin(/*formatIfFail=*/true, /*readOnly=*/false);
}

// Minimal query-string value extractor -- avoids pulling in a JSON/URL
// parsing library for one key at a time.
std::string queryParam(httpd_req_t* req, const char* key) {
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) return "";
    std::vector<char> buf(qlen + 1);
    if (httpd_req_get_url_query_str(req, buf.data(), buf.size()) != ESP_OK) return "";
    char val[256] = {};
    if (httpd_query_key_value(buf.data(), key, val, sizeof(val)) != ESP_OK) return "";
    return std::string(val);
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

esp_err_t handleLittleFsList(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    if (!ensureLfs()) {
        return httpd_resp_send(req, "{\"total\":0,\"used\":0,\"entries\":[]}", HTTPD_RESP_USE_STRLEN);
    }

    std::string dir = queryParam(req, "dir");
    if (dir.empty()) dir = "/";

    size_t total = 0, used = 0;
    s_lfs.getSpace(total, used);
    auto entries = s_lfs.list(dir);

    std::string json = "{\"total\":" + std::to_string(total) + ",\"used\":" + std::to_string(used) + ",\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i) json += ",";
        json += "{\"name\":\"" + jsonEscape(entries[i].name) + "\",\"isDir\":" +
                (entries[i].isDir ? "true" : "false") + ",\"size\":" + std::to_string(entries[i].size) + "}";
    }
    json += "]}";
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t handleLittleFsDownload(httpd_req_t* req) {
    std::string file = queryParam(req, "file");
    if (file.empty() || !ensureLfs()) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    if (file[0] != '/') file = "/" + file;

    httpd_resp_set_type(req, "application/octet-stream");
    bool ok = s_lfs.readChunks(file, [req](const uint8_t* data, size_t len) {
        return httpd_resp_send_chunk(req, (const char*)data, len) == ESP_OK;
    });
    if (!ok) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, nullptr, 0); // terminate chunked response
    return ESP_OK;
}

esp_err_t handleLittleFsDelete(httpd_req_t* req) {
    std::string file = queryParam(req, "file");
    if (file.empty() || !ensureLfs()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing file");
        return ESP_FAIL;
    }
    if (file[0] != '/') file = "/" + file;

    if (!s_lfs.removeFile(file)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "delete failed");
        return ESP_FAIL;
    }
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Buffers the whole upload in RAM before writing -- fine for the capture
// file sizes this device actually produces.
esp_err_t handleLittleFsUpload(httpd_req_t* req) {
    std::string file = queryParam(req, "file");
    if (file.empty() || !ensureLfs()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing file");
        return ESP_FAIL;
    }
    if (file[0] != '/') file = "/" + file;

    std::string body;
    body.reserve(req->content_len);
    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, std::min((int)sizeof(buf), remaining));
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload read failed");
            return ESP_FAIL;
        }
        body.append(buf, n);
        remaining -= n;
    }

    if (!s_lfs.write(file, body, /*append=*/false)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
        return ESP_FAIL;
    }
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t handleWs(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        // WebSocket handshake -- the one point with a real httpd_req_t to
        // pull the socket fd from for later async sends.
        s_wsFd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "ws client connected, fd=%d", s_wsFd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws recv (len probe) failed: %s", esp_err_to_name(err));
        return err;
    }
    if (frame.len == 0) return ESP_OK;

    std::vector<uint8_t> buf(frame.len + 1);
    frame.payload = buf.data();
    err = httpd_ws_recv_frame(req, &frame, buf.size());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws recv (payload) failed: %s", esp_err_to_name(err));
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "ws client closed, fd=%d", req->handle ? httpd_req_to_sockfd(req) : -1);
        s_wsFd = -1;
        return ESP_OK;
    }
    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        feedCharQueue(frame.payload, frame.len);
    }
    return ESP_OK;
}

bool startHttpServer(const std::string& apIp, ITerminalView& log) {
    if (s_httpd) return true;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    DnsServer::configureCaptiveDns(config); // also sets max_uri_handlers -- overridden below, this file registers 9
    config.max_open_sockets = 4;
    config.max_uri_handlers = 12; // headroom above the 9 actually registered
    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        log.println("webui: httpd_start failed: " + std::string(esp_err_to_name(err)));
        s_httpd = nullptr;
        return false;
    }

    httpd_uri_t uriIndex = { "/", HTTP_GET, handleIndex, nullptr };
    httpd_uri_t uriStyle = { "/style.css", HTTP_GET, handleStyle, nullptr };
    httpd_uri_t uriScripts = { "/scripts.js", HTTP_GET, handleScripts, nullptr };
    httpd_uri_t uriWs = {};
    uriWs.uri = "/ws";
    uriWs.method = HTTP_GET;
    uriWs.handler = handleWs;
    uriWs.is_websocket = true;

    httpd_uri_t uriLfsList = { "/littlefs/list", HTTP_GET, handleLittleFsList, nullptr };
    httpd_uri_t uriLfsDownload = { "/littlefs/download", HTTP_GET, handleLittleFsDownload, nullptr };
    httpd_uri_t uriLfsDelete = { "/littlefs/delete", HTTP_DELETE, handleLittleFsDelete, nullptr };
    httpd_uri_t uriLfsUpload = { "/littlefs/upload", HTTP_POST, handleLittleFsUpload, nullptr };

    httpd_register_uri_handler(s_httpd, &uriIndex);
    httpd_register_uri_handler(s_httpd, &uriStyle);
    httpd_register_uri_handler(s_httpd, &uriScripts);
    httpd_register_uri_handler(s_httpd, &uriWs);
    httpd_register_uri_handler(s_httpd, &uriLfsList);
    httpd_register_uri_handler(s_httpd, &uriLfsDownload);
    httpd_register_uri_handler(s_httpd, &uriLfsDelete);
    httpd_register_uri_handler(s_httpd, &uriLfsUpload);

    // Wildcard registered last so it only catches what the routes above didn't.
    s_captivePortalUrl = "http://" + apIp + "/";
    httpd_uri_t uriCaptive = { "/*", HTTP_GET, handleCaptivePortal, nullptr };
    httpd_register_uri_handler(s_httpd, &uriCaptive);
    return true;
}

} // namespace

void start(ITerminalView& log) {
    if (s_apUp) {
        log.println("webui: already running, SSID \"" + std::string(WiFi.softAPSSID().c_str()) + "\"");
        log.println(std::string("webui: connect to it and browse to http://") + WiFi.softAPIP().toString().c_str());
        return;
    }

    // WiFi.mode()/softAP() below bring up the SDIO link to the C6 for the
    // first time -- if its firmware is incompatible with arduino-esp32
    // 3.3.12, this is where it would hang/abort, not recoverable here.
    //
    // SSID/password/channel/max-connections match the original's own
    // WifiTypeConfigurator.cpp exactly, not a Tab5-specific choice.
    static constexpr const char* kApSsid = "ESP32-Bit-Pirate";
    static constexpr const char* kApPassword = "readytoboard";
    static constexpr int kApChannel = 6;
    static constexpr int kApMaxConnections = 4;

    log.println(std::string("webui: starting AP \"") + kApSsid + "\"...");
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    IPAddress apIp(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, gateway, subnet);

    bool ok = WiFi.softAP(kApSsid, kApPassword, kApChannel, false, kApMaxConnections);
    if (!ok) {
        log.println("webui: WiFi.softAP() failed");
        return;
    }
    s_apUp = true;

    IPAddress ip = WiFi.softAPIP();
    std::string ipStr = ip.toString().c_str();
    log.println(std::string("webui: AP up, SSID \"") + kApSsid + "\" / password \"" + kApPassword + "\"");
    log.println("webui: browse to http://" + ipStr);

    if (!startHttpServer(ipStr, log)) return;
    DnsServer::startCaptiveDns(ipStr);

    Tab5TerminalView::setWebMirror(sendToWsClient);
    log.println("webui: HTTP + WebSocket server + captive portal running");
}

bool isRunning() { return s_apUp && s_httpd != nullptr; }

} // namespace webui

// Extern hook for ActionDispatcher.cpp's "webui" case -- see that call
// site's comment. Global scope on purpose: must match the extern
// declaration there, which has no namespace qualifier.
void bitpirate_webui_start_hook(ITerminalView& terminalView) {
    webui::start(terminalView);
}
