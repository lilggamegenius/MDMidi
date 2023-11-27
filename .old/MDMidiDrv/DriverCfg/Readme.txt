mid2smps MIDI Driver Readme
---------------------------
This MIDI driver was made to make the life of mid2smps (like me :P) users easier.
It will play MIDIs in the exact same way, as if you would convert them with mid2smps and insert them into a Sonic 1 MegaDrive ROM.

Features:
- limitations like on real hardware (i.e. monophonic channels, up to 10 notes simultaneously)
- support for 2612edit/mid2smps GYB instrument libraries
- support for mid2smps PSG envelope lists and mapping files
- support for DAC sounds, both uncompressed (PCM) and compressed (jman2050/DPCM)
- programmable modulation (SMPS 68k style) and other SMPS effects 
- dynamic switching between FM6 and DAC*
- 3x PSG melody + 1x PSG fixed-frequency noise possible** (needs to be set up via MIDI Controllers)
- SMPS frequency limits

* not possible with an unmodified Sonic 1 sound driver
** see * and not possible with mid2smps

Notes:
- When using the PSG noise for drums, some envelopes (like the one that's often used for a Open Hi-Hat) run infinitely.
  You can turn them off forcibly by playing any PSG drum with a velocity <16.
  Else the MIDI driver will stop the note after 5 seconds (mid2smps doesn't do that!) or when sending an All Notes Off controller (ID 123) on the respective channel.
- If your MIDI sequencer supports it, it's recommended to enable "send reset controllers when stopping".

Bugs:
- Windows is unable to find the driver DLL, if its path contains spaces.
  This seems to be an issue of Windows itself, so the only solution is to use paths without spaces.
  (i.e. the root- or system32 directory)

Credits:
- kode54 and Mudlord (they wrote the BASSMIDI driver, which this MIDI driver is based on)
- MAME Team (YM2612 and SN76489 sound chip emulators)
- Me (sound engine, DAC streaming, resampling code from VGMPlay, mid2smps)

Happy music making!
Valley Bell
