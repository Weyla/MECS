/* HTTP is a view/controller adapter. It never reads GPIOs or sends CAN frames.
 * Commands require a same-origin custom header; no permissive CORS is enabled.
 * Access is intended for a trusted LAN, not direct Internet exposure. */
#include "master.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
static char browser_key[33];

static esp_err_t index_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' "
                       "'unsafe-inline'; frame-ancestors 'none'; connect-src 'self'");
    return httpd_resp_send(req, (const char *)index_start, index_end - index_start);
}
static esp_err_t state(httpd_req_t *req)
{
    char *json = master_json();
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot unavailable");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-MIO-Key", browser_key);
    esp_err_t error = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return error;
}
static esp_err_t command(httpd_req_t *req)
{
    char key[sizeof(browser_key)];
    if (httpd_req_get_hdr_value_str(req, "X-MIO-Key", key, sizeof(key)) != ESP_OK ||
        strcmp(key, browser_key)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                   "Reload the dashboard before sending commands");
    }
    char line[160];
    if (req->content_len <= 0 || req->content_len >= sizeof(line)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid command length");
    }
    size_t used = 0;
    while (used < req->content_len) {
        int count = httpd_req_recv(req, line + used, req->content_len - used);
        if (count <= 0) {
            return ESP_FAIL;
        }
        used += count;
    }
    line[used] = 0;
    for (size_t i = 0; i < used; ++i) {
        if ((unsigned char)line[i] < 32 || (unsigned char)line[i] > 126) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "Use one printable command line");
        }
    }
    char result[160];
    bool ok = master_command(line, result, sizeof(result));
    /* Do not echo rejected text: a user may accidentally paste credentials
     * into this console instead of the USB-only provisioning command. */
    if (ok) {
        master_note("Web command accepted: %s", line);
    } else {
        master_note("Web command rejected; inspect command syntax and node state");
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, ok ? "202 Accepted" : "400 Bad Request");
    return httpd_resp_send(req, result, HTTPD_RESP_USE_STRLEN);
}
void web_start(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        snprintf(browser_key + i * 8, 9, "%08lx", (unsigned long)esp_random());
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_page},
        {.uri = "/api/state", .method = HTTP_GET, .handler = state},
        {.uri = "/api/command", .method = HTTP_POST, .handler = command},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
}
