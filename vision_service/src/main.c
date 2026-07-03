#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vision_service.h"

#define VISION_UVC_DEV_PATH       "/dev/video0"
#define VISION_UVC_PIX_FMT        "MJPEG"
#define VISION_UVC_WIDTH          1280
#define VISION_UVC_HEIGHT         720
#define VISION_UVC_TIMEOUT_MS     2000
#define VISION_TELEMETRY_PORT     9100
#define VISION_CONTROL_PORT       9101
#define VISION_SNAPSHOT_EVERY     30
#define VISION_SNAPSHOT_LIMIT     100

static void vision_signal_handler(int signo)
{
    (void)signo;
    vision_service_request_stop();
}

static td_s32 vision_install_signal_handlers(td_void)
{
    struct sigaction action;

    (void)memset(&action, 0, sizeof(action));
    action.sa_handler = vision_signal_handler;
    (void)sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, TD_NULL) != 0 ||
        sigaction(SIGTERM, &action, TD_NULL) != 0) {
        perror("sigaction");
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_bool vision_parse_u32(const td_char *text, td_u32 *value)
{
    char *end = TD_NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return TD_FALSE;
    }
    *value = (td_u32)parsed;
    return TD_TRUE;
}

static td_bool vision_parse_endpoint(td_char *endpoint, td_char **host, td_u16 *port)
{
    td_char *separator = strrchr(endpoint, ':');
    td_u32 parsed_port;

    if (separator == TD_NULL || separator == endpoint || separator[1] == '\0') {
        return TD_FALSE;
    }
    *separator = '\0';
    if (vision_parse_u32(separator + 1, &parsed_port) != TD_TRUE ||
        parsed_port == 0 || parsed_port > UINT16_MAX) {
        return TD_FALSE;
    }
    *host = endpoint;
    *port = (td_u16)parsed_port;
    return TD_TRUE;
}

static void vision_print_usage(const td_char *program)
{
    printf("Usage: %s [options]\n"
        "  --device PATH             UVC device (default: %s)\n"
        "  --format NAME             MJPEG/H264/H265/YUYV (default: %s)\n"
        "  --width N                 capture width (default: %u)\n"
        "  --height N                capture height (default: %u)\n"
        "  --telemetry IP:PORT       send per-frame JSON over UDP\n"
        "  --snapshot-dir PATH       save NV21, annotated PPM and JSON\n"
        "  --snapshot-every N        save every Nth frame (default: %u)\n"
        "  --snapshot-limit N        maximum saved frames, 0=unlimited (default: %u)\n"
        "  --mpp-attached            use existing MPP SYS/VB owned by vo_init\n"
        "  --display                 initialize HDMI VO/GFBG in this process\n"
        "  --display-ready-file PATH write file after display is ready\n"
        "  --control-port N          listen for enable/disable monitoring commands (default: %u)\n"
        "  --monitoring-start-disabled wait for a control command before inference\n"
        "  --monitoring-start-enabled  run inference immediately\n",
        program, VISION_UVC_DEV_PATH, VISION_UVC_PIX_FMT,
        VISION_UVC_WIDTH, VISION_UVC_HEIGHT,
        VISION_SNAPSHOT_EVERY, VISION_SNAPSHOT_LIMIT,
        VISION_CONTROL_PORT);
}

int main(int argc, char *argv[])
{
    vision_service_config config = {
        .device_path = VISION_UVC_DEV_PATH,
        .pixel_format = VISION_UVC_PIX_FMT,
        .width = VISION_UVC_WIDTH,
        .height = VISION_UVC_HEIGHT,
        .capture_timeout_ms = VISION_UVC_TIMEOUT_MS,
        .telemetry_host = TD_NULL,
        .telemetry_port = VISION_TELEMETRY_PORT,
        .snapshot_dir = TD_NULL,
        .snapshot_every = VISION_SNAPSHOT_EVERY,
        .snapshot_limit = VISION_SNAPSHOT_LIMIT,
        .mpp_attached = TD_FALSE,
        .display_enable = TD_FALSE,
        .display_ready_file = TD_NULL,
        .monitoring_default = TD_TRUE,
        .control_port = VISION_CONTROL_PORT,
    };
    static const struct option options[] = {
        {"device", required_argument, TD_NULL, 'd'},
        {"format", required_argument, TD_NULL, 'f'},
        {"width", required_argument, TD_NULL, 'w'},
        {"height", required_argument, TD_NULL, 'h'},
        {"telemetry", required_argument, TD_NULL, 't'},
        {"snapshot-dir", required_argument, TD_NULL, 's'},
        {"snapshot-every", required_argument, TD_NULL, 'e'},
        {"snapshot-limit", required_argument, TD_NULL, 'l'},
        {"mpp-attached", no_argument, TD_NULL, 'a'},
        {"display", no_argument, TD_NULL, 'p'},
        {"display-ready-file", required_argument, TD_NULL, 'r'},
        {"control-port", required_argument, TD_NULL, 'c'},
        {"monitoring-start-disabled", no_argument, TD_NULL, 1000},
        {"monitoring-start-enabled", no_argument, TD_NULL, 1001},
        {"help", no_argument, TD_NULL, '?'},
        {TD_NULL, 0, TD_NULL, 0}
    };
    td_char *telemetry_endpoint = TD_NULL;
    td_s32 option;

    setvbuf(stdout, TD_NULL, _IONBF, 0);
    setvbuf(stderr, TD_NULL, _IONBF, 0);

    while ((option = getopt_long(argc, argv, "d:f:w:h:t:s:e:l:apr:c:?", options, TD_NULL)) != -1) {
        switch (option) {
            case 'd':
                config.device_path = optarg;
                break;
            case 'f':
                config.pixel_format = optarg;
                break;
            case 'w':
                if (vision_parse_u32(optarg, &config.width) != TD_TRUE || config.width == 0) {
                    fprintf(stderr, "invalid width: %s\n", optarg);
                    return 2;
                }
                break;
            case 'h':
                if (vision_parse_u32(optarg, &config.height) != TD_TRUE || config.height == 0) {
                    fprintf(stderr, "invalid height: %s\n", optarg);
                    return 2;
                }
                break;
            case 't':
                telemetry_endpoint = optarg;
                if (vision_parse_endpoint(telemetry_endpoint,
                    (td_char **)&config.telemetry_host, &config.telemetry_port) != TD_TRUE) {
                    fprintf(stderr, "invalid telemetry endpoint: %s\n", optarg);
                    return 2;
                }
                break;
            case 's':
                config.snapshot_dir = optarg;
                break;
            case 'e':
                if (vision_parse_u32(optarg, &config.snapshot_every) != TD_TRUE ||
                    config.snapshot_every == 0) {
                    fprintf(stderr, "invalid snapshot interval: %s\n", optarg);
                    return 2;
                }
                break;
            case 'l':
                if (vision_parse_u32(optarg, &config.snapshot_limit) != TD_TRUE) {
                    fprintf(stderr, "invalid snapshot limit: %s\n", optarg);
                    return 2;
                }
                break;
            case 'a':
                config.mpp_attached = TD_TRUE;
                break;
            case 'p':
                config.display_enable = TD_TRUE;
                break;
            case 'r':
                config.display_ready_file = optarg;
                break;
            case 'c': {
                td_u32 port;
                if (vision_parse_u32(optarg, &port) != TD_TRUE ||
                    port == 0 || port > UINT16_MAX) {
                    fprintf(stderr, "invalid control port: %s\n", optarg);
                    return 2;
                }
                config.control_port = (td_u16)port;
                break;
            }
            case 1000:
                config.monitoring_default = TD_FALSE;
                break;
            case 1001:
                config.monitoring_default = TD_TRUE;
                break;
            default:
                vision_print_usage(argv[0]);
                return (option == '?') ? 0 : 2;
        }
    }

    if (vision_install_signal_handlers() != TD_SUCCESS) {
        return 1;
    }

    return (vision_service_run(&config) == TD_SUCCESS) ? 0 : 1;
}
