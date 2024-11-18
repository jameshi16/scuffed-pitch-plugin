/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <RubberBandStretcher.h>
#include <obs-module.h>
#include <obs.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static RubberBand::RubberBandStretcher
    rubberband(48000, 2,
               RubberBand::RubberBandStretcher::OptionProcessRealTime |
                   RubberBand::RubberBandStretcher::OptionPitchHighQuality);

static struct obs_audio_data *process_audio(void *, obs_audio_data *audio) {
  rubberband.process((float **)audio->data, audio->frames, false);

  if ((size_t) rubberband.available() < audio->frames) {
    obs_log(LOG_INFO, "Rubberband isn't ready, zeroing");
    for (int c = 0; c < MAX_AV_PLANES; c++) {
      if (audio->data[c] != nullptr) {
        for (size_t i = 0; i < audio->frames; i++) {
          audio->data[c][i] = 0.0;
        }
      }
    }

    return audio;
  }

  rubberband.retrieve((float **)audio->data, audio->frames);

  return audio;
}

struct obs_source_info pitch_shift_filter = {
    .id = "pitch_shift_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name = [](void *) { return "Pitch Shift Filter"; },
    .create = [](obs_data *, obs_source *) { return (void *)'c'; },
    .destroy = [](void *) {},
    .update = [](void *, obs_data *) {},
    .filter_audio = process_audio,
};

bool obs_module_load(void) {
  rubberband.setPitchScale(2.0);
  obs_register_source(&pitch_shift_filter);
  obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
  return true;
}

void obs_module_unload(void) { obs_log(LOG_INFO, "plugin unloaded"); }
