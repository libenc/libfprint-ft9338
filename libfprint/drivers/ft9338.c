#include "ft9338.h"

#include <ctype.h>
#include <stdint.h>

#define FP_COMPONENT "ft9338"

#include "fpi-log.h"
#include "fpi-image.h"
#include "fpi-image-device.h"
#include "fpi-ssm.h"
#include "fpi-usb-transfer.h"

G_DEFINE_TYPE (FpiDeviceFt9338, fpi_device_ft9338, FP_TYPE_IMAGE_DEVICE)

static const FpIdEntry id_table[] = {
  { .vid = 0x2808,  .pid = 0x9338,  .driver_data = 0 },
  { .vid = 0,       .pid = 0,       .driver_data = 0 },
};

typedef void (*SynCmdMsgCallback) (FpiDeviceFt9338 *self,
                                   uint8_t         *buffer_in,
                                   gsize            length_in,
                                   GError          *error);

typedef struct
{
  SynCmdMsgCallback callback;
} CommandData;

typedef struct
{
  const CtrlCmd          **cmds;
  const SynCmdMsgCallback *cmd_msg_cbs;
  size_t                   num_cmds;
} CmdSsmData;



/* ############## shared funcs ############## */

static void
transfer_cmd_cb (
  FpiUsbTransfer *transfer,
  FpDevice       *dev,
  gpointer        user_data,
  GError         *error
                )
{
  G_DEBUG_HERE ();
  g_return_if_fail (transfer->ssm);

  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }

  CommandData *cmd_data = user_data;
  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);

  fp_dbg ("[transfer_cmd_cb] response %d length: %ld", transfer->request, transfer->actual_length);

  if (cmd_data->callback)
    {
      fp_dbg ("[transfer_cmd_cb] calling callback: %p", cmd_data->callback);
      // ssm state must be handled in callback
      cmd_data->callback (self, transfer->buffer, transfer->actual_length, error);
    }
  else
    {
      fp_dbg ("[transfer_cmd_cb] no callback, moving to next state");
      fpi_ssm_next_state (transfer->ssm);
    }
  g_clear_pointer (&cmd_data, g_free);
}

// when all the control commands sequence are done, move task_ssm to the next state
static void
cmd_ssm_default_cb (
  FpiSsm   *cmd_ssm,
  FpDevice *dev,
  GError   *error
                   )
{
  G_DEBUG_HERE ();

  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
  fp_dbg ("[cmd_ssm_default_cb] moving task_ssm to next state");
  fpi_ssm_next_state (self->task_ssm);
};

static void
send_ctrl_cmd (
  FpDevice *dev, const CtrlCmd *cmd, FpiSsm *ssm, SynCmdMsgCallback cmd_msg_cb
              )
{
  G_DEBUG_HERE ();

  CommandData *cmd_data = g_new0 (CommandData, 1);
  cmd_data->callback = cmd_msg_cb;

  FpiUsbTransfer *transfer;
  transfer = fpi_usb_transfer_new (dev);
  transfer->short_is_error = TRUE;

  fpi_usb_transfer_fill_control (
    transfer,
    cmd->direction,
    cmd->request_type,
    cmd->recipient,
    cmd->request,
    cmd->value,
    cmd->idx,
    cmd->length
                                );
  transfer->ssm = ssm;
  fpi_usb_transfer_submit (transfer, FT9338_CMD_TIMEOUT_MS, NULL, transfer_cmd_cb, cmd_data);
};

static void
send_ctrl_seq_ssm_handler (
  FpiSsm *cmd_ssm, FpDevice *dev
                          )
{
  G_DEBUG_HERE ();

  CmdSsmData *data = fpi_ssm_get_data (cmd_ssm);
  int cur_state = fpi_ssm_get_cur_state (cmd_ssm);
  g_assert (data && cur_state <= data->num_cmds - 1);

  if (data->cmds[cur_state] == NULL)
    {
      fp_dbg ("[send_ctrl_seq_ssm_handler] cmd in state %d is NULL, moving cmd_ssm to next state", cur_state);
      fpi_ssm_next_state (cmd_ssm);
      return;
    }
  else
    {
      fp_dbg ("[send_ctrl_seq_ssm_handler] sending control command %d: %d", cur_state, data->cmds[cur_state]->request);
      send_ctrl_cmd (dev, data->cmds[cur_state], cmd_ssm, data->cmd_msg_cbs[cur_state]);
    }
};

// send multiple control commands in sequence
// if `cmd_ssm_cb` is NULL, the default callback will move the task_ssm to the next state
static void
send_ctrl_seq (
  FpDevice *dev, const CtrlCmd **cmds, const SynCmdMsgCallback *cmd_msg_cbs, size_t num_cmds, FpiSsmCompletedCallback cmd_ssm_cb
              )
{
  G_DEBUG_HERE ();

  // use cmd_ssm to loop through the cmds
  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
  CmdSsmData *data = g_new0 (CmdSsmData, 1);
  data->cmds = cmds;
  data->cmd_msg_cbs = cmd_msg_cbs;
  data->num_cmds = num_cmds;

  self->cmd_ssm = fpi_ssm_new (dev, send_ctrl_seq_ssm_handler, num_cmds);
  fpi_ssm_set_data (self->cmd_ssm, data, g_free);
  fpi_ssm_start (self->cmd_ssm, cmd_ssm_cb ? (FpiSsmCompletedCallback) cmd_ssm_cb : cmd_ssm_default_cb);
};

static void
send_bulk (
  FpDevice         *dev,
  const BulkCmd    *cmd,
  FpiSsm           *ssm,
  SynCmdMsgCallback cmd_msg_cb
          )
{
  G_DEBUG_HERE ();

  CommandData *cmd_data = g_new0 (CommandData, 1);
  cmd_data->callback = cmd_msg_cb;

  FpiUsbTransfer *transfer;
  transfer = fpi_usb_transfer_new (dev);
  transfer->short_is_error = TRUE;

  fp_dbg ("[send_bulk] endpoint: %d, length: %ld", cmd->endpoint, cmd->length);
  fpi_usb_transfer_fill_bulk (
    transfer,
    cmd->endpoint,
    cmd->length
                             );
  transfer->ssm = ssm;
  fp_dbg ("[send_bulk] submitting bulk transfer");
  fpi_usb_transfer_submit (transfer, FT9338_CMD_TIMEOUT_MS, NULL, transfer_cmd_cb, cmd_data);
};



/* ############## cmd callbacks ############## */

static void
dev_open_init_58_cb (
  FpiDeviceFt9338 *self,
  uint8_t         *buffer_in,
  gsize            length_in,
  GError          *error
                    )
{
  G_DEBUG_HERE ();
  if (error)
    {
      fpi_ssm_mark_failed (self->cmd_ssm, error);
      return;
    }

  fp_dbg ("[dev_open_init_58_cb] checking device status");
  fp_dbg ("[dev_open_init_58_cb] %02x %02x %02x %02x", buffer_in[0], buffer_in[1], buffer_in[2], buffer_in[3]);
  // unknown device status
  if (
    (buffer_in[0] == 0x0a && buffer_in[1] == 0x0a && buffer_in[2] == 0x00 && buffer_in[3] == 0x00) ||
    (buffer_in[0] == 0x02 && buffer_in[1] == 0x02 && buffer_in[2] == 0x00 && buffer_in[3] == 0x00)
     )
    {
      fp_dbg ("[dev_open_init_58_cb] device ready");
      fpi_ssm_next_state (self->cmd_ssm);
    }
  else
    {
      fp_dbg ("[dev_open_init_58_cb] device not ready, retrying...");
      // back to cmd_ssm state 0 to retry
      fpi_ssm_jump_to_state_delayed (self->cmd_ssm, 0, 200);
    }
}

static void
dev_open_bulk_long_info_cb (
  FpiDeviceFt9338 *self,
  uint8_t         *buffer_in,
  gsize            length_in,
  GError          *error
                           )
{
  G_DEBUG_HERE ();

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  // set width and height
  self->width = buffer_in[23];
  self->height = buffer_in[24];

  fp_dbg ("[dev_open_bulk_long_info_cb] image size: width = %d, height = %d", self->width, self->height);
  fpi_ssm_next_state (self->task_ssm);
};

static void
dev_activate_bulk_short_info_cb (
  FpiDeviceFt9338 *self,
  uint8_t         *buffer_in,
  gsize            length_in,
  GError          *error
                                )
{
  G_DEBUG_HERE ();
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_dbg ("[dev_activate_bulk_short_info_cb] %02x %02x %02x %02x", buffer_in[0], buffer_in[1], buffer_in[2], buffer_in[3]);
  // if all bytes are 0x02, it means the device is not ready
  if (buffer_in[0] == 0x02 && buffer_in[1] == 0x02 && buffer_in[2] == 0x02 && buffer_in[3] == 0x02)
    {
      // this should abort the activation
      fp_err ("[dev_activate_bulk_short_info_cb] device not ready");
      fpi_image_device_session_error (FP_IMAGE_DEVICE (self), NULL);
      if (self->task_ssm)
        fpi_ssm_mark_failed (self->task_ssm, NULL);
    }
  else
    {
      fpi_ssm_next_state (self->task_ssm);
    }
}

static void
probe_fp_67_cb (
  FpiDeviceFt9338 *self,
  uint8_t         *buffer_in,
  gsize            length_in,
  GError          *error
               )
{
  G_DEBUG_HERE ();
  if (error)
    {
      fpi_ssm_mark_failed (self->cmd_ssm, error);
      return;
    }

  if (self->is_deactivating == TRUE)
    {
      fp_dbg ("[probe_fp_67_cb] deactivation detected, stopping");
      fpi_ssm_mark_completed (self->cmd_ssm);
      fpi_ssm_jump_to_state (self->task_ssm, CAPTURE_FP_STATE_FINGER_UP);
      return;
    }

  fp_dbg ("[probe_fp_67_cb] %02x %02x %02x %02x", buffer_in[0], buffer_in[1], buffer_in[2], buffer_in[3]);
  if (buffer_in[1] != 0x43 || (buffer_in[0] != 0x00 && buffer_in[0] != 0x01))
    {
      fp_err ("[probe_fp_67_cb] device not in correct state while capturing fingerprint");
      fpi_image_device_session_error (FP_IMAGE_DEVICE (self), NULL);
      if (self->task_ssm)
        fpi_ssm_mark_failed (self->task_ssm, NULL);
      return;
    }

  if (buffer_in[0] == 0x00)
    {
      fpi_ssm_jump_to_state (self->cmd_ssm, 0);
    }
  else
    {
      fp_dbg ("[probe_fp_67_cb] fingerprint captured");
      fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (self), TRUE);
      fpi_ssm_next_state (self->cmd_ssm);
    }
}

static void
capture_bulk_get_fp_cb (
  FpiDeviceFt9338 *self,
  uint8_t         *buffer_in,
  gsize            length_in,
  GError          *error
                       )
{
  G_DEBUG_HERE ();
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_dbg ("[capture_bulk_get_fp_cb] %d %d", self->width, self->height);
  FpImage *img = fp_image_new (self->width, self->height);
  size_t img_size = self->width * self->height;
  img->flags |= FPI_IMAGE_PARTIAL;
  // img->ppmm = 9; // this shouldn't be hardcoded, but we don't know the way to get it from device

  memcpy (img->data, buffer_in, img_size);
  fpi_image_device_image_captured (FP_IMAGE_DEVICE (self), img);

  fpi_ssm_next_state (self->task_ssm);
}

static const SynCmdMsgCallback *ctrl_seq_init_cbs = (SynCmdMsgCallback[]){
  NULL, NULL, dev_open_init_58_cb
};

static const SynCmdMsgCallback *ctrl_seq_ping_cbs = (SynCmdMsgCallback[]){
  NULL, NULL, NULL
};

static const SynCmdMsgCallback *ctrl_seq_long_info_cbs = (SynCmdMsgCallback[]){
  NULL, NULL, NULL
};

static const SynCmdMsgCallback *ctrl_seq_short_info_cbs = (SynCmdMsgCallback[]){
  NULL, NULL, NULL
};

static const SynCmdMsgCallback *ctrl_seq_probe_fp_cbs = (SynCmdMsgCallback[]){
  NULL, probe_fp_67_cb
};

static const SynCmdMsgCallback *ctrl_seq_get_fp_cbs = (SynCmdMsgCallback[]){
  NULL, NULL, NULL
};

static const SynCmdMsgCallback *ctrl_seq_short_fp_cbs = (SynCmdMsgCallback[]){
  NULL, NULL, NULL
};



/* ############ cmd ssm callbacks ############ */

static void
probe_fp_cmd_ssm_cb (
  FpiSsm   *ssm,
  FpDevice *dev,
  GError   *error
                    )
{
  G_DEBUG_HERE ();

  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);

  if(self->is_deactivating)
    {
      fp_dbg ("[probe_fp_cmd_ssm_cb] deactivating, moving to finger up state");
      fpi_ssm_jump_to_state (self->task_ssm, CAPTURE_FP_STATE_FINGER_UP);
    }
  else
    {
      fpi_ssm_next_state (self->task_ssm);
    }
};



/* ############## ssm handlers ############## */

static void
dev_open_ssm_handler (FpiSsm *task_ssm, FpDevice *dev)
{
  G_DEBUG_HERE ();

  switch (fpi_ssm_get_cur_state (task_ssm))
    {
    case DEV_OPEN_STATE_26:
      send_ctrl_cmd (dev, &ctrl_cmd_26, task_ssm, NULL);
      break;

    case DEV_OPEN_STATE_INIT:
      send_ctrl_seq (
        dev,
        ctrl_seq_init,
        ctrl_seq_init_cbs,
        3,
        NULL
                    );
      break;

    case DEV_OPEN_STATE_GET_SIZE:
      send_ctrl_seq (
        dev,
        ctrl_seq_long_info,
        ctrl_seq_long_info_cbs,
        3,
        NULL
                    );
      break;

    case DEV_OPEN_STATE_GET_SIZE_BULK:
      send_bulk (dev, &bulk_cmd_long_info, task_ssm, dev_open_bulk_long_info_cb);
      break;
    }
}

static void
dev_activate_ssm_handler (FpiSsm *task_ssm, FpDevice *dev)
{
  G_DEBUG_HERE ();

  switch (fpi_ssm_get_cur_state (task_ssm))
    {
    case DEV_ACTIVATE_STATE_PING:
      send_ctrl_seq (
        dev,
        ctrl_seq_ping,
        ctrl_seq_ping_cbs,
        3,
        NULL
                    );
      break;

    // currently we simply check long info as "activating"
    case DEV_ACTIVATE_STATE_GET_SHORT_INFO:
      send_ctrl_seq (
        dev,
        ctrl_seq_short_info,
        ctrl_seq_short_info_cbs,
        3,
        NULL
                    );
      break;

    case DEV_ACTIVATE_STATE_GET_SHORT_INFO_BULK:
      send_bulk (dev, &bulk_cmd_long_info, task_ssm, dev_activate_bulk_short_info_cb);
      break;
    }
}

static void
capture_fp_ssm_handler (FpiSsm *task_ssm, FpDevice *dev)
{
  G_DEBUG_HERE ();

  switch (fpi_ssm_get_cur_state (task_ssm))
    {
    case CAPTURE_FP_STATE_CLEAR:
      send_ctrl_seq (
        dev,
        ctrl_seq_short_fp,
        ctrl_seq_short_fp_cbs,
        3,
        NULL
                    );
      break;

    case CAPTURE_FP_STATE_CLEAR_BULK:
      send_bulk (dev, &bulk_cmd_reset_fp, task_ssm, NULL);
      break;

    case CAPTURE_FP_STATE_PROBE:
      send_ctrl_seq (
        dev,
        ctrl_seq_probe_fp,
        ctrl_seq_probe_fp_cbs,
        2,
        probe_fp_cmd_ssm_cb
                    );
      break;

    case CAPTURE_FP_STATE_CAPTURE:
      send_ctrl_seq (
        dev,
        ctrl_seq_get_fp,
        ctrl_seq_get_fp_cbs,
        3,
        NULL
                    );
      break;

    case CAPTURE_FP_STATE_CAPTURE_BULK:
      send_bulk (dev, &bulk_cmd_get_fp, task_ssm, capture_bulk_get_fp_cb);
      break;

    case CAPTURE_FP_STATE_FINGER_UP:
      fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (dev), FALSE);
      FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
      if (self->is_deactivating)
        fpi_image_device_deactivate_complete (FP_IMAGE_DEVICE (self), NULL);
      fpi_ssm_next_state (task_ssm);
      break;
    }
}



/* ########### task ssm callbacks ########### */

static void
dev_open_ssm_cb (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  G_DEBUG_HERE ();
  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
  self->task_ssm = NULL;
  fpi_image_device_open_complete (FP_IMAGE_DEVICE (dev), error);
};

static void
dev_activate_ssm_cb (
  FpiSsm   *ssm,
  FpDevice *dev,
  GError   *error
                    )
{
  G_DEBUG_HERE ();

  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
  self->task_ssm = NULL;
  fpi_image_device_activate_complete (FP_IMAGE_DEVICE (self), NULL);
};

static void
capture_fp_ssm_cb (
  FpiSsm   *ssm,
  FpDevice *dev,
  GError   *error
                  )
{
  G_DEBUG_HERE ();

  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
  self->task_ssm = NULL;
};



/* ############## entry points ############## */

static void
ft9338_img_open (FpImageDevice *dev)
{
  G_DEBUG_HERE ();
  GError *error = NULL;
  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);

  // reset
  self->is_deactivating = FALSE;

  // set configuation
  if (!g_usb_device_set_configuration (fpi_device_get_usb_device (FP_DEVICE (dev)), 1, &error))
    {
      fp_err ("[ft9338_img_open] Failed to set configuration: %s", error->message);
      fpi_image_device_open_complete (dev, error);
    }

  // claim interface
  if (!g_usb_device_claim_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, &error))
    {
      fp_err ("[ft9338_img_open] Failed to claim interface: %s", error->message);
      fpi_image_device_open_complete (dev, error);
    }

  self->task_ssm = fpi_ssm_new (FP_DEVICE (self), dev_open_ssm_handler, DEV_OPEN_STATE_NUM_STATES);
  fpi_ssm_start (self->task_ssm, dev_open_ssm_cb);
}

static void
ft9338_img_close (FpImageDevice *dev)
{
  G_DEBUG_HERE ();

  g_usb_device_release_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, NULL);
  fpi_image_device_close_complete (dev, NULL);
}

static void
ft9338_activate (FpImageDevice *dev)
{
  G_DEBUG_HERE ();
  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);

  self->is_deactivating = FALSE;
  self->task_ssm = fpi_ssm_new (FP_DEVICE (self), dev_activate_ssm_handler, DEV_ACTIVATE_STATE_NUM_STATES);
  fpi_ssm_start (self->task_ssm, dev_activate_ssm_cb);
}

static void
ft9338_deactivate (FpImageDevice *dev)
{
  G_DEBUG_HERE ();

  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);

  // if task_ssm is not exist, it means the device is already deactivated
  if (self->task_ssm == NULL)
    {
      fp_dbg ("[ft9338_deactivate] task_ssm is NULL, device already deactivated");
      fpi_image_device_deactivate_complete (dev, NULL);
    }
  else
    {
      self->is_deactivating = TRUE;
    }
}

static void
ft9338_change_state (FpImageDevice *dev, FpiImageDeviceState state)
{
  G_DEBUG_HERE ();

  FpiDeviceFt9338 *self = FPI_DEVICE_FT9338 (dev);
  FpiImageDeviceState prev_state = self->prev_state;
  self->prev_state = state;

  if (state != FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON || prev_state != FPI_IMAGE_DEVICE_STATE_IDLE)
    {
      fp_dbg ("[ft9338_change_state] not 'idle -> await-finger-on', ignored");
      return;
    }

  fp_dbg ("[ft9338_change_state] 'idle -> await-finger-on', starting to probe fingerprint");
  self->task_ssm = fpi_ssm_new (FP_DEVICE (self), capture_fp_ssm_handler, CAPTURE_FP_STATE_NUM_STATES);
  fpi_ssm_start (self->task_ssm, capture_fp_ssm_cb);
}



/* ################## init ################## */

static void
fpi_device_ft9338_init (FpiDeviceFt9338 *self)
{
  G_DEBUG_HERE ();
}

static void
fpi_device_ft9338_class_init (FpiDeviceFt9338Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = FT9338_DRIVER_FULLNAME;
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = 10;

  //// use _FpiDeviceFt9338::width/height instead
  // img_class->img_width = -1;
  // img_class->img_height = -1;

  img_class->img_open = ft9338_img_open;
  img_class->img_close = ft9338_img_close;

  img_class->activate = ft9338_activate;
  img_class->deactivate = ft9338_deactivate;
  img_class->change_state = ft9338_change_state;
}
