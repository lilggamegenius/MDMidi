#pragma once

void ym2612_update_request(void *param);

void ym2612_stream_update(UINT8 ChipID, stream_sample_t **outputs, int samples);
int device_start_ym2612(UINT8 ChipID, int clock);
void device_stop_ym2612(UINT8 ChipID);
void device_reset_ym2612(UINT8 ChipID);

//UINT8 ym2612_r(UINT8 ChipID, offs_t offset);
void ym2612_w(UINT8 ChipID, offs_t offset, UINT8 data);

void ym2612_set_mute_mask(UINT8 ChipID, UINT32 MuteMask);
