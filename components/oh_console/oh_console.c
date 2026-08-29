#include "oh_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "oh_app.h"
#include "oh_board.h"
#include "oh_clock.h"
#include "oh_config.h"
#include "oh_json.h"
#include "oh_net.h"
#include "oh_node.h"
#include "oh_vmod.h"
#include "sdkconfig.h"

static const char *TAG = "oh_console";

static void print_json(cJSON *json)
{
    char *text = cJSON_Print(json);
    cJSON_Delete(json);
    if (text) {
        printf("%s\n", text);
        free(text);
    }
}

static int cmd_info(void)
{
    char name[OH_NAME_MAX], ip[16], ssid[33], ap[24];
    oh_app_node_name(name, sizeof(name));
    oh_net_ip(ip, sizeof(ip));
    oh_net_ssid(ssid, sizeof(ssid));
    oh_net_ap_ssid(ap, sizeof(ap));
    printf("node      %s  \"%s\"\n", oh_app_sn(), name);
    printf("firmware  %s on %s\n", oh_app_fw(), oh_board_name());
    printf("hostname  %s.local\n", oh_app_hostname());
    printf("network   %s %s%s%s%s%s\n", oh_net_transport(), ip, ssid[0] ? "  ssid " : "", ssid,
           ap[0] ? "  setup AP " : "", ap);
    printf("clock     %s, up %lu s\n", oh_clock_is_unix() ? "NTP" : "since boot", (unsigned long)oh_clock_uptime_s());
    oh_config_t cfg;
    oh_config_get(&cfg);
    printf("broker    %s\n", cfg.mqtt_broker[0] ? cfg.mqtt_broker : "(none: not adopted)");
    oh_node_lock();
    const oh_module_t *m = oh_node_module();
    const oh_port_state_t *st = oh_node_states();
    printf("module    0x%04x \"%s\" rev %s flags 0x%lx (%s)%s\n", m->module.type, m->module.name, m->module.rev,
           (unsigned long)m->module.flags, oh_app_module_source(), oh_module_switches_mains(m) ? "  MAINS" : "");
    if (m->has_dmx) {
        printf("dmx       %s, %u universe(s), configured %s/%u\n", m->dmx.protocols[0], m->dmx.universes,
               cfg.dmx_protocol, cfg.dmx_universe);
    }
    for (int i = 0; i < m->n_ports; i++) {
        const oh_port_desc_t *p = &m->ports[i];
        cJSON *v = oh_json_value(&st[i].value);
        char *text = cJSON_PrintUnformatted(v);
        cJSON_Delete(v);
        printf("  port %-2u %-20s %-9s %-8s %s\n", p->port, p->name, oh_access_str(p->access),
               oh_datatype_str(p->type), text ? text : "");
        free(text);
    }
    oh_node_unlock();
    char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    size_t n = oh_node_errors(codes, OH_MAX_ERRORS);
    printf("errors    ");
    for (size_t i = 0; i < n; i++) {
        printf("%s ", codes[i]);
    }
    printf("%s\n", n ? "" : "none");
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        esp_err_t err = oh_config_set_wifi(argv[2], argc >= 4 ? argv[3] : "");
        printf("%s\n", err == ESP_OK ? "ok, connecting" : esp_err_to_name(err));
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        oh_config_set_wifi("", "");
        printf("ok\n");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "scan") == 0) {
        print_json(oh_net_wifi_scan());
        return 0;
    }
    printf("usage: oh wifi set <ssid> [psk] | oh wifi clear | oh wifi scan\n");
    return 1;
}

static int cmd_cfg(int argc, char **argv)
{
    if (argc == 1) {
        print_json(oh_config_to_json());
        return 0;
    }
    if (strcmp(argv[1], "mqtt") == 0) {
        oh_config_set_mqtt_broker(argc >= 3 ? argv[2] : "");
    } else if (strcmp(argv[1], "name") == 0) {
        oh_config_set_name(argc >= 3 ? argv[2] : "");
    } else if (strcmp(argv[1], "dmx") == 0 && argc >= 4) {
        if (!oh_node_has_dmx()) {
            printf("this node forwards no universe\n");
            return 1;
        }
        oh_config_set_dmx(argv[2], (uint16_t)atoi(argv[3]));
    } else if (strcmp(argv[1], "json") == 0 && argc >= 3) {
        cJSON *body = cJSON_Parse(argv[2]);
        if (!body) {
            printf("not JSON\n");
            return 1;
        }
        char err[OH_ERR_MAX];
        uint32_t changed;
        oh_node_lock();
        esp_err_t rc = oh_config_apply_json(body, oh_node_module(), err, &changed);
        oh_node_unlock();
        cJSON_Delete(body);
        printf("%s\n", rc == ESP_OK ? "ok" : err);
        return rc == ESP_OK ? 0 : 1;
    } else {
        printf("usage: oh cfg | oh cfg mqtt <host:port> | oh cfg name <name> | oh cfg dmx <proto> <universe> | oh cfg json '<body>'\n");
        return 1;
    }
    printf("ok\n");
    return 0;
}

static int cmd_vmod(int argc, char **argv)
{
    char err[OH_ERR_MAX] = "";
    if (argc >= 2 && strcmp(argv[1], "show") == 0) {
        oh_node_lock();
        cJSON *o = oh_vmod_to_json(oh_node_module());
        oh_node_unlock();
        print_json(o);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "presets") == 0) {
        size_t n;
        const char *const *keys = oh_vmod_preset_keys(&n);
        for (size_t i = 0; i < n; i++) {
            printf("%s ", keys[i]);
        }
        printf("\n");
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "preset") == 0) {
        esp_err_t rc = oh_app_set_preset(argv[2], err);
        printf("%s\n", rc == ESP_OK ? "ok" : err);
        return rc == ESP_OK ? 0 : 1;
    }
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        esp_err_t rc = oh_app_set_vmod_json(argv[2], err);
        printf("%s\n", rc == ESP_OK ? "ok" : err);
        return rc == ESP_OK ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        oh_app_clear_vmod();
        printf("ok\n");
        return 0;
    }
    printf("usage: oh vmod show | presets | preset <key> | set '<json>' | clear\n");
    return 1;
}

static int cmd_in(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: oh in <port> <0|1|number|text>\n");
        return 1;
    }
    uint8_t port = (uint8_t)atoi(argv[1]);
    oh_port_desc_t desc;
    if (oh_node_get(port, &desc, NULL) != ESP_OK) {
        printf("no port %u\n", port);
        return 1;
    }
    oh_value_t v = {.type = desc.type};
    switch (desc.type) {
    case OH_DT_BOOLEAN:
        v.state = atoi(argv[2]) != 0;
        break;
    case OH_DT_NUMBER:
        v.value = strtof(argv[2], NULL);
        break;
    case OH_DT_STRING:
        strncpy(v.text, argv[2], OH_TEXT_MAX - 1);
        break;
    case OH_DT_COLOR:
        sscanf(argv[2], "%hhu,%hhu,%hhu", &v.color.r, &v.color.g, &v.color.b);
        break;
    }
    esp_err_t err = oh_node_inject_input(port, &v);
    printf("%s\n", err == ESP_OK ? "ok" : "not an input");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_out(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: oh out <port> <0|1|number|text|r,g,b|{json}>\n");
        return 1;
    }
    uint8_t port = (uint8_t)atoi(argv[1]);
    oh_port_desc_t desc;
    if (oh_node_get(port, &desc, NULL) != ESP_OK) {
        printf("no port %u\n", port);
        return 1;
    }
    oh_value_t v = {.type = desc.type};
    if (argv[2][0] == '{') {
        cJSON *j = cJSON_Parse(argv[2]);
        bool ok = j && oh_json_value_parse(desc.type, j, &v);
        cJSON_Delete(j);
        if (!ok) {
            printf("payload does not fit a %s port\n", oh_datatype_str(desc.type));
            return 1;
        }
    } else {
        switch (desc.type) {
        case OH_DT_BOOLEAN:
            v.state = atoi(argv[2]) != 0 || strcmp(argv[2], "on") == 0;
            break;
        case OH_DT_NUMBER:
            v.value = strtof(argv[2], NULL);
            break;
        case OH_DT_STRING:
            strncpy(v.text, argv[2], OH_TEXT_MAX - 1);
            break;
        case OH_DT_COLOR:
            sscanf(argv[2], "%hhu,%hhu,%hhu", &v.color.r, &v.color.g, &v.color.b);
            break;
        }
    }
    esp_err_t err = oh_node_set_output(port, &v, OH_SOURCE_CONSOLE);
    printf("%s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_oh(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        printf("oh info                       everything about this node\n"
               "oh wifi set <ssid> [psk]      store credentials and connect\n"
               "oh wifi clear | scan\n"
               "oh cfg [mqtt <host:port> | name <name> | dmx <proto> <universe> | json '<body>']\n"
               "oh vmod show | presets | preset <key> | set '<json>' | clear\n"
               "oh in <port> <value>          pretend an input happened\n"
               "oh out <port> <value>         drive an output\n"
               "oh identify | reboot | reset\n");
        return 0;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "info") == 0) {
        return cmd_info();
    }
    if (strcmp(sub, "wifi") == 0) {
        return cmd_wifi(argc - 1, argv + 1);
    }
    if (strcmp(sub, "cfg") == 0) {
        return cmd_cfg(argc - 1, argv + 1);
    }
    if (strcmp(sub, "vmod") == 0) {
        return cmd_vmod(argc - 1, argv + 1);
    }
    if (strcmp(sub, "in") == 0) {
        return cmd_in(argc - 1, argv + 1);
    }
    if (strcmp(sub, "out") == 0) {
        return cmd_out(argc - 1, argv + 1);
    }
    if (strcmp(sub, "identify") == 0) {
        oh_node_identify(5000);
        return 0;
    }
    if (strcmp(sub, "reboot") == 0) {
        oh_app_reboot(200);
        return 0;
    }
    if (strcmp(sub, "reset") == 0) {
        oh_app_factory_reset();
        return 0;
    }
    printf("unknown: oh %s (try oh help)\n", sub);
    return 1;
}

esp_err_t oh_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "oh>";
    repl_config.max_cmdline_length = 4096;
    repl_config.task_stack_size = 12288;
    esp_err_t err;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_serial_jtag(&hw, &repl_config, &repl);
#else
    esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(&hw, &repl_config, &repl);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "repl: %s", esp_err_to_name(err));
        return err;
    }
    esp_console_register_help_command();
    const esp_console_cmd_t cmd = {.command = "oh", .help = "OpenHaunt node: oh help", .func = cmd_oh};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    return esp_console_start_repl(repl);
}
