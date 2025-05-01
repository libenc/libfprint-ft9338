

#pragma once

#include "fpi-image-device.h"
#include "fpi-ssm.h"
#include "fpi-usb-transfer.h"

#include <stdio.h>
#include <stdlib.h>

G_DECLARE_FINAL_TYPE (FpiDeviceFt9338, fpi_device_ft9338, FPI, DEVICE_FT9338, FpImageDevice)

#define FT9338_DRIVER_FULLNAME "Focaltech FT9338 Fingerprint Sensor"
#define FT9338_CMD_TIMEOUT_MS 1000

/* ############### cmd structs ############### */

typedef struct CtrlCmd
{
  GUsbDeviceDirection   direction;
  GUsbDeviceRequestType request_type;
  GUsbDeviceRecipient   recipient;
  guint8                request; // bRequest
  guint16               value; // wValue
  guint16               idx; // wIndex
  gsize                 length; // wLength, in bytes
} CtrlCmd;

typedef struct BulkCmd
{
  guint8 endpoint;
  gsize  length;
} BulkCmd;


/* ################# cmds ################# */

static const CtrlCmd ctrl_cmd_26 = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 26,
  .value = 0x0,
  .idx = 0x0,
  .length = 4,
};

static const CtrlCmd ctrl_cmd_34 = {
  .direction = G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 34,
  .value = 0x70,
  .idx = 0x70,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_58 = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 58,
  .value = 0x0,
  .idx = 0x20,
  .length = 4,
};

static const CtrlCmd ctrl_cmd_52_1 = {
  .direction = G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 52,
  .value = 0xff,
  .idx = 0x0,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_52_2 = {
  .direction = G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 52,
  .value = 0x3,
  .idx = 0x0,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_67 = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 67,
  .value = 0x0,
  .idx = 0x0,
  .length = 4,
};

static const CtrlCmd ctrl_cmd_111_ping = {
  .direction = G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 111,
  .value = 0x0,
  .idx = 0xff00,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_111_short_info = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 111,
  .value = 0x4,
  .idx = 0x9180,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_111_long_info = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 111,
  .value = 0x20,
  .idx = 0x9180,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_111_reset_fp = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 111,
  .value = 0x6,
  .idx = 0x9080,
  .length = 0,
};

static const CtrlCmd ctrl_cmd_111_get_fp = {
  .direction = G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
  .request_type = G_USB_DEVICE_REQUEST_TYPE_VENDOR,
  .recipient = G_USB_DEVICE_RECIPIENT_DEVICE,
  .request = 111,
  .value = 0x1400,
  .idx = 0x9080,
  .length = 0,
};

static const BulkCmd bulk_cmd_short_info = {
  .endpoint = 0x83,
  .length = 4,
};

static const BulkCmd bulk_cmd_long_info = {
  .endpoint = 0x83,
  .length = 32,
};

static const BulkCmd bulk_cmd_reset_fp = {
  .endpoint = 0x83,
  .length = 6,
};

static const BulkCmd bulk_cmd_get_fp = {
  .endpoint = 0x83,
  .length = 5120,
};


/* ################ cmd seqs ################ */

static const CtrlCmd *ctrl_seq_init[] = {
  &ctrl_cmd_34,
  &ctrl_cmd_34,
  &ctrl_cmd_58,
};

static const CtrlCmd *ctrl_seq_ping[] = {
  &ctrl_cmd_52_1,
  &ctrl_cmd_52_2,
  &ctrl_cmd_111_ping,
};

static const CtrlCmd *ctrl_seq_short_info[] = {
  &ctrl_cmd_52_1,
  &ctrl_cmd_52_2,
  &ctrl_cmd_111_short_info,
};

static const CtrlCmd *ctrl_seq_long_info[] = {
  &ctrl_cmd_52_1,
  &ctrl_cmd_52_2,
  &ctrl_cmd_111_long_info,
};

static const CtrlCmd *ctrl_seq_short_fp[] = {
  &ctrl_cmd_52_1,
  &ctrl_cmd_52_2,
  &ctrl_cmd_111_reset_fp,
};

static const CtrlCmd *ctrl_seq_get_fp[] = {
  &ctrl_cmd_52_1,
  &ctrl_cmd_52_2,
  &ctrl_cmd_111_get_fp,
};

static const CtrlCmd *ctrl_seq_probe_fp[] = {
  NULL,
  &ctrl_cmd_67,
};

/* ############### ssm states ############### */

typedef enum {
  DEV_OPEN_STATE_26 = 0,
  DEV_OPEN_STATE_INIT,
  DEV_OPEN_STATE_GET_SIZE,
  DEV_OPEN_STATE_GET_SIZE_BULK,
  DEV_OPEN_STATE_NUM_STATES,
} DevOpenState;

typedef enum {
  DEV_ACTIVATE_STATE_PING = 0,
  DEV_ACTIVATE_STATE_GET_SHORT_INFO,
  DEV_ACTIVATE_STATE_GET_SHORT_INFO_BULK,
  DEV_ACTIVATE_STATE_NUM_STATES,
} DevActiveState;

typedef enum {
  CAPTURE_FP_STATE_CLEAR = 0,
  CAPTURE_FP_STATE_CLEAR_BULK,
  CAPTURE_FP_STATE_PROBE,
  CAPTURE_FP_STATE_CAPTURE,
  CAPTURE_FP_STATE_CAPTURE_BULK,
  CAPTURE_FP_STATE_FINGER_UP,
  CAPTURE_FP_STATE_NUM_STATES,
} CaptureFpState;


struct _FpiDeviceFt9338
{
  FpDevice            parent;
  FpiSsm             *task_ssm;
  FpiSsm             *cmd_ssm;
  gint                width;
  gint                height;
  gboolean            is_deactivating;
  FpiImageDeviceState prev_state;
};
