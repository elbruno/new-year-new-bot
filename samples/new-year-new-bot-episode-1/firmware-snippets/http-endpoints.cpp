// Selected rows from the firmware route table; other routes are omitted.
const struct { const char* path; httpd_method_t method; } routes[] = {
    {"/api/status", HTTP_GET},
    {"/api/actions", HTTP_POST},
    {"/api/version", HTTP_GET},
};

// The firmware registers each route with the shared request handler.
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
