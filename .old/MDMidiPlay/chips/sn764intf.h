#pragma once

void sn764xx_stream_update(UINT8 ChipID, stream_sample_t **outputs, int samples);

int device_start_sn764xx(UINT8 ChipID, int clock, int shiftregwidth, int noisetaps,
						 int negate, int stereo, int clockdivider, int freq0);
void device_stop_sn764xx(UINT8 ChipID);
void device_reset_sn764xx(UINT8 ChipID);

void sn764xx_w(UINT8 ChipID, offs_t offset, UINT8 data);

void sn764xx_set_mute_mask(UINT8 ChipID, UINT32 MuteMask);
