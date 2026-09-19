/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Transcribe a raw 16kHz mono s16le file. Used to verify the whisper path
 * end to end without needing someone at the microphone.
 *
 * Run:  ./build/spike/hear_test models/ggml-base.en-q5_1.bin clip.raw
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "whisper.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: hear_test <model> <raw16k>\n"); return 2; }

    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
    std::vector<short> raw;
    short s;
    while (fread(&s, sizeof s, 1, f) == 1) raw.push_back(s);
    fclose(f);

    std::vector<float> audio(raw.size());
    double energy = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        audio[i] = raw[i] / 32768.0f;
        energy += double(audio[i]) * audio[i];
    }
    printf("%zu samples (%.2fs), rms %.4f\n", audio.size(), audio.size() / 16000.0,
           std::sqrt(energy / (audio.empty() ? 1 : audio.size())));

    whisper_log_set([](ggml_log_level, const char *, void *) {}, nullptr);
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    whisper_context *ctx = whisper_init_from_file_with_params(argv[1], cp);
    if (!ctx) { fprintf(stderr, "model load failed\n"); return 1; }

    whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wp.print_progress = wp.print_realtime = wp.print_timestamps = false;
    wp.language = "en";
    wp.n_threads = 8;
    wp.no_context = true;

    const int64_t t0 = whisper_full(ctx, wp, audio.data(), (int)audio.size());
    if (t0 != 0) { fprintf(stderr, "transcription failed\n"); return 1; }

    std::string text;
    for (int i = 0; i < whisper_full_n_segments(ctx); i++)
        text += whisper_full_get_segment_text(ctx, i);
    printf("transcript: \"%s\"\n", text.c_str());

    whisper_free(ctx);
    return 0;
}
