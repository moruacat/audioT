// sstv_timing.h - SSTV timing parameter constants
#ifndef AUDIOT_SSTV_TIMING_H
#define AUDIOT_SSTV_TIMING_H

namespace audiot {
namespace sstv {

// ---- Calibration / header tones ----
// Leader (calibration header) tone
const float LEADER_FREQ_HZ   = 1900.0f;
const float LEADER_TIME_MS   = 300.0f;

// Break tone between leader and VIS
const float BREAK_FREQ_HZ    = 1200.0f;
const float BREAK_TIME_MS    = 10.0f;

// ---- VIS (Vertical Interval Signaling) ----
// VIS start bit
const float VIS_START_FREQ_HZ = 1200.0f;
const float VIS_START_TIME_MS = 300.0f;
// VIS data bit duration
const float VIS_BIT_TIME_MS   = 30.0f;
// VIS data bit frequencies (bit=1 / bit=0)
const float VIS_BIT_ONE_HZ    = 1100.0f;
const float VIS_BIT_ZERO_HZ   = 1300.0f;
// VIS stop bit
const float VIS_STOP_FREQ_HZ  = 1200.0f;
const float VIS_STOP_TIME_MS  = 30.0f;
// Number of VIS data bits (7 data + 1 even parity)
const int VIS_DATA_BITS       = 7;

// ---- Pixel scan (FM luminance/chroma) ----
// Sync pulse (all modes)
const float SYNC_FREQ_HZ      = 1200.0f;
// Pixel scan frequency range
const float BLACK_FREQ_HZ     = 1500.0f;
const float WHITE_FREQ_HZ     = 2300.0f;
// Frequency span per full brightness range
const float SCAN_SPAN_HZ      = WHITE_FREQ_HZ - BLACK_FREQ_HZ;

// ---- Detection thresholds ----
// Relative tolerance when matching tone frequencies during detection
const float FREQ_TOLERANCE    = 0.06f;   // +/- 6%
// Minimum leader duration accepted during detection (ms)
const float MIN_LEADER_MS     = 100.0f;

} // namespace sstv
} // namespace audiot
#endif // AUDIOT_SSTV_TIMING_H
