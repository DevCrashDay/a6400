#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/**
 * Sony Alpha PTP/IP Client for Localhost Camera Control
 * Talks to Sony PTP/IP server on localhost:15740
 * Supports: shutter capture, bulb exposure, focus stepping, and exposure compensation.
 */

#define PTPIP_PORT 15740
#define PTPIP_HOST "127.0.0.1"

// PTP/IP Packet Types
#define PTPIP_INIT_CMD_REQ      1
#define PTPIP_INIT_CMD_ACK      2
#define PTPIP_INIT_EVENT_REQ    3
#define PTPIP_INIT_EVENT_ACK    4
#define PTPIP_CMD_REQUEST       6
#define PTPIP_CMD_RESPONSE      7
#define PTPIP_EVENT             8

// Standard & Sony PTP Operation Codes
#define PTP_OC_OpenSession           0x1002
#define PTP_OC_CloseSession          0x1003
#define PTP_OC_InitiateCapture       0x100E
#define PTP_OC_Sony_SDIOConnect      0x9201
#define PTP_OC_Sony_GetDeviceProp    0x9202
#define PTP_OC_Sony_SetDeviceProp    0x9203
#define PTP_OC_Sony_ActAutoFocus     0x9207
#define PTP_OC_Sony_ActShutter       0x9208
#define PTP_OC_Sony_StepFocus        0x9205

#pragma pack(push, 1)
typedef struct {
    uint32_t length;
    uint32_t type;
} PtpIpHeader;

typedef struct {
    PtpIpHeader hdr;
    uint8_t guid[16];
    char friendly_name[32];
    uint32_t protocol_version;
} PtpIpInitCmdReq;

typedef struct {
    PtpIpHeader hdr;
    uint32_t connection_number;
    uint8_t guid[16];
    char camera_name[32];
    uint32_t protocol_version;
} PtpIpInitCmdAck;

typedef struct {
    PtpIpHeader hdr;
    uint32_t data_phase;
    uint16_t op_code;
    uint32_t transaction_id;
    uint32_t params[5];
} PtpIpCmdReq;

typedef struct {
    PtpIpHeader hdr;
    uint16_t response_code;
    uint32_t transaction_id;
    uint32_t params[5];
} PtpIpCmdResp;
#pragma pack(pop)

static int g_sock = -1;
static uint32_t g_trans_id = 1;

int ptpip_connect(const char *host, int port) {
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        perror("[PTP/IP] socket failed");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(g_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[PTP/IP] Could not connect to %s:%d (%s)\n", host, port, strerror(errno));
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    // Send Init_Command_Request
    PtpIpInitCmdReq req;
    memset(&req, 0, sizeof(req));
    req.hdr.length = sizeof(req);
    req.hdr.type = PTPIP_INIT_CMD_REQ;
    for (int i = 0; i < 16; i++) req.guid[i] = (uint8_t)(i + 0x20);
    strncpy(req.friendly_name, "a6400_Homebrew", sizeof(req.friendly_name));
    req.protocol_version = 0x00010000;

    if (send(g_sock, &req, sizeof(req), 0) != sizeof(req)) {
        fprintf(stderr, "[PTP/IP] Init command failed\n");
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    // Receive Init_Command_Ack
    uint8_t ack_buf[256];
    int n = recv(g_sock, ack_buf, sizeof(ack_buf), 0);
    if (n < 8) {
        fprintf(stderr, "[PTP/IP] No ack from camera PTP/IP server\n");
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    printf("[PTP/IP] Connected to Sony PTP/IP server at %s:%d\n", host, port);
    return g_sock;
}

int ptpip_send_cmd(uint16_t op_code, int num_params, uint32_t p1, uint32_t p2, uint32_t p3) {
    if (g_sock < 0) return -1;

    PtpIpCmdReq req;
    memset(&req, 0, sizeof(req));
    uint32_t payload_len = 12 + num_params * 4;
    req.hdr.length = sizeof(PtpIpHeader) + payload_len;
    req.hdr.type = PTPIP_CMD_REQUEST;
    req.data_phase = 1; // Command without data
    req.op_code = op_code;
    req.transaction_id = g_trans_id++;
    req.params[0] = p1;
    req.params[1] = p2;
    req.params[2] = p3;

    int total_send = req.hdr.length;
    if (send(g_sock, &req, total_send, 0) != total_send) {
        fprintf(stderr, "[PTP/IP] Failed to send command 0x%04x\n", op_code);
        return -1;
    }

    // Read Response
    PtpIpCmdResp resp;
    int n = recv(g_sock, &resp, sizeof(resp), 0);
    if (n >= 8) {
        return (resp.response_code == 0x2001) ? 0 : resp.response_code; // 0x2001 = PTP_RC_OK
    }
    return 0; // Return success on clean write
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Sony a6400 PTP/IP Control Client\n");
        printf("Usage:\n");
        printf("  sony_ptpip shoot                   # Single shutter release\n");
        printf("  sony_ptpip bulb_start              # Open shutter (bulb start)\n");
        printf("  sony_ptpip bulb_stop               # Close shutter (bulb end)\n");
        printf("  sony_ptpip focus_step [near|far] [1-5] # Drive lens focus step\n");
        printf("  sony_ptpip set_ev [val]            # Set EV compensation\n");
        return 1;
    }

    const char *cmd = argv[1];
    const char *host = PTPIP_HOST;

    if (ptpip_connect(host, PTPIP_PORT) < 0) {
        fprintf(stderr, "[PTP/IP] Cannot reach localhost:%d.\n", PTPIP_PORT);
        fprintf(stderr, "Ensure 'Ctrl w/ Smartphone' is enabled in camera menu.\n");
        return 1;
    }

    if (strcmp(cmd, "shoot") == 0) {
        printf("[PTP/IP] Triggering shutter capture...\n");
        ptpip_send_cmd(PTP_OC_Sony_ActAutoFocus, 1, 2, 0, 0); // AF On
        usleep(150000);
        ptpip_send_cmd(PTP_OC_Sony_ActShutter, 1, 2, 0, 0);   // Shutter Press
        usleep(200000);
        ptpip_send_cmd(PTP_OC_Sony_ActShutter, 1, 1, 0, 0);   // Shutter Release
        ptpip_send_cmd(PTP_OC_Sony_ActAutoFocus, 1, 1, 0, 0); // AF Off
        printf("[SUCCESS] Capture command sent.\n");
    } else if (strcmp(cmd, "bulb_start") == 0) {
        printf("[PTP/IP] Starting bulb exposure...\n");
        ptpip_send_cmd(PTP_OC_Sony_ActShutter, 1, 2, 0, 0);   // Keep shutter pressed
        printf("[SUCCESS] Shutter opened.\n");
    } else if (strcmp(cmd, "bulb_stop") == 0) {
        printf("[PTP/IP] Stopping bulb exposure...\n");
        ptpip_send_cmd(PTP_OC_Sony_ActShutter, 1, 1, 0, 0);   // Release shutter
        printf("[SUCCESS] Shutter closed.\n");
    } else if (strcmp(cmd, "focus_step") == 0) {
        int dir = 1; // 1 = near, 2 = far
        int step = 1;
        if (argc >= 3 && strcmp(argv[2], "far") == 0) dir = 2;
        if (argc >= 4) step = atoi(argv[3]);
        printf("[PTP/IP] Stepping focus: %s (magnitude %d)...\n", dir == 1 ? "NEAR" : "FAR", step);
        ptpip_send_cmd(PTP_OC_Sony_StepFocus, 2, dir, step, 0);
        printf("[SUCCESS] Focus step command sent.\n");
    } else if (strcmp(cmd, "set_ev") == 0) {
        int ev = (argc >= 3) ? atoi(argv[2]) : 0;
        printf("[PTP/IP] Setting EV compensation: %d...\n", ev);
        ptpip_send_cmd(PTP_OC_Sony_SetDeviceProp, 2, 0x5010, ev, 0);
        printf("[SUCCESS] EV set.\n");
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
    }

    close(g_sock);
    return 0;
}
