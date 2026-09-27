/* Frame-stream recorder (W4 apparatus): the consumer artifact of §3.3 that
 * replay (R-08) reads. Wall-clock values are recorded as metadata only and
 * are never used for positioning (R-07). */
#ifndef EA_RECORDER_H
#define EA_RECORDER_H

#include "edge_audio/edge_audio.h"

typedef struct ea_recorder ea_recorder;

ea_recorder *ea_recorder_open(const char *path, const ea_stream_info *info,
                              uint32_t frame_samples);
int ea_recorder_write(ea_recorder *r, const ea_frame *f, uint64_t wall_us);
void ea_recorder_close(ea_recorder *r);

#endif /* EA_RECORDER_H */
