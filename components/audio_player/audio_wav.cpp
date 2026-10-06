#include <string.h>
#include "audio_wav.h"

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

// The board's codec path supports PCM16 mono/stereo. Treat RIFF lengths as
// untrusted, including metadata padding and the declared data boundary.
bool is_wav(FILE *fp, wav_instance *instance) {
    if (!fp || !instance) return false;
    *instance = {};
    if (fseek(fp, 0, SEEK_END)) return false;
    long length = ftell(fp);
    if (length < 12 || fseek(fp, 0, SEEK_SET)) return false;
    uint8_t riff[12];
    if (fread(riff, 1, 12, fp) != 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return false;
    uint64_t end = (uint64_t)le32(riff + 4) + 8;
    if (end < 12 || end > (uint64_t)length) return false;
    bool have_format = false;
    uint64_t pos = 12;
    while (pos + 8 <= end) {
        uint8_t chunk[8];
        if (fread(chunk, 1, 8, fp) != 8) return false;
        uint32_t size = le32(chunk + 4);
        pos += 8;
        uint64_t next = pos + size + (size & 1);
        if (next > end) return false;
        if (!memcmp(chunk, "fmt ", 4)) {
            uint8_t fmt[16];
            if (have_format || size < 16 || fread(fmt, 1, 16, fp) != 16) return false;
            unsigned channels = le16(fmt + 2), rate = le32(fmt + 4);
            unsigned align = le16(fmt + 12), bits = le16(fmt + 14);
            if (le16(fmt) != 1 || (channels != 1 && channels != 2) || bits != 16 ||
                rate < 8000 || rate > 48000 || align != channels * 2 || le32(fmt + 8) != rate * align)
                return false;
            instance->header.AudioFormat = 1;
            instance->header.NumChannels = channels;
            instance->header.SampleRate = rate;
            instance->header.BitsPerSample = bits;
            instance->header.BlockAlign = align;
            have_format = true;
        } else if (!memcmp(chunk, "data", 4)) {
            if (!have_format || size % instance->header.BlockAlign) return false;
            instance->data_remaining = size;
            return true; // fp points at the first PCM frame.
        }
        if (fseek(fp, (long)next, SEEK_SET)) return false;
        pos = next;
    }
    return false;
}

DECODE_STATUS decode_wav(FILE *fp, decode_data *data, wav_instance *instance) {
    if (!fp || !data || !instance) return DECODE_STATUS_ERROR;
    data->frame_count = 0;
    size_t align = instance->header.BlockAlign;
    if ((align != 2 && align != 4) || !data->samples || data->samples_capacity < align)
        return DECODE_STATUS_ERROR;
    if (!instance->data_remaining) return DECODE_STATUS_DONE;
    size_t count = data->samples_capacity / align * align;
    if (count > instance->data_remaining) count = instance->data_remaining;
    size_t got = fread(data->samples, 1, count, fp);
    if (got != count) return DECODE_STATUS_ERROR;
    instance->data_remaining -= got;
    data->fmt = {instance->header.SampleRate, (uint32_t)instance->header.BitsPerSample,
                 (uint32_t)instance->header.NumChannels};
    data->frame_count = got / align;
    return DECODE_STATUS_CONTINUE;
}
