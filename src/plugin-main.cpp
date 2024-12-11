/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redist
ribute it and/or modify
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
#include <algorithm>
#include <crow.h>
#include <crow/middlewares/cors.h>
#include <memory>
#include <obs-data.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <obs.h>
#include <plugin-support.h>
#include <thread>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

struct WebPitchFilter {
  std::recursive_mutex mutex;
  std::mutex audio_mutex;
  std::unique_ptr<RubberBand::RubberBandStretcher> rubberband;
  std::unique_ptr<crow::App<crow::CORSHandler>> app;
  std::unique_ptr<std::thread> thread;
  uint16_t port;
  std::string addr;

  WebPitchFilter(size_t sample_rate, size_t channels, uint16_t port = 8085,
                 std::string addr = "0.0.0.0")
      : port(port), addr(addr), app(new crow::App<crow::CORSHandler>()) {
    this->rubberband = std::make_unique<RubberBand::RubberBandStretcher>(
        sample_rate, channels,
        RubberBand::RubberBandStretcher::OptionProcessRealTime |
            RubberBand::RubberBandStretcher::OptionThreadingNever |
            RubberBand::RubberBandStretcher::OptionWindowShort |
            RubberBand::RubberBandStretcher::OptionPitchHighConsistency |
            RubberBand::RubberBandStretcher::OptionEngineFiner |
            RubberBand::RubberBandStretcher::OptionSmoothingOn);
  }

  void start_thread_server() {
    std::lock_guard<std::recursive_mutex> lock(this->mutex);

    this->app.reset(new crow::App<crow::CORSHandler>());

    crow::App<crow::CORSHandler> &crowApp = *(this->app.get());
    auto &cors = crowApp.get_middleware<crow::CORSHandler>();
    cors.global().methods("GET"_method).origin("*");
    CROW_ROUTE(crowApp, "/")
        .methods(crow::HTTPMethod::GET)(
            [this](const crow::request &req, crow::response &res) {
              res.add_header("Access-Control-Allow-Origin", "*");
              res.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");

              if (req.url_params.get("pitch") != nullptr) {
                const auto pitch = std::stof(req.url_params.get("pitch"));
                // NOTE: Clamped pitch, because any higher might cause desyncs
                this->rubberband->setPitchScale(
                    std::max(0.75f, std::min(pitch, 1.5f)));
                this->rubberband->reset();
              }
              res.end();
            });
    this->app->port(this->port).bindaddr(this->addr);
    this->thread.reset(new std::thread([this]() {
      try {
        this->app->run();
      } catch (const std::system_error &e) {
        obs_log(LOG_ERROR,
                "Error in server thread, probably port is already used %s",
                e.what());
      }
    }));
  }

  void stop_thread_server() {
    obs_log(LOG_INFO, "stopping the thread server %d",
            std::this_thread::get_id());
    std::lock_guard<std::recursive_mutex> lock(this->mutex);

    // NOTE: assumption that stop is thread-safe
    obs_log(LOG_INFO, "Stopping app server");
    if (this->thread && this->thread->joinable()) {
      // TODO: When the server is stop, it does not release the binding, which
      // means that I cannot restart the server.

      this->app->stop();
      this->thread->join();
      free(this->app.release());
    }
  }
};

static struct obs_audio_data *process_audio(void *data, obs_audio_data *audio) {
  WebPitchFilter *filter = reinterpret_cast<WebPitchFilter *>(data);
  std::lock_guard<std::mutex> lock(filter->audio_mutex);
  auto rubberband = filter->rubberband.get();
  float** audiodata = (float**)audio->data;
  rubberband->process(audiodata, audio->frames, false);

  if ((size_t)rubberband->available() < audio->frames) {
    obs_log(LOG_INFO, "Rubberband isn't ready, zeroing");
    for (int c = 0; c < MAX_AV_PLANES; c++) {
      if (audiodata[c] != nullptr) {
        for (size_t i = 0; i < audio->frames; i++) {
          audiodata[c][i] = 0.0;
        }
      }
    }

    return audio;
  }

  rubberband->retrieve(audiodata, audio->frames);

  // HACK: Mono the audio to bypass synchronization issues
  for (int c = 1; c < MAX_AV_PLANES; c++) {
    if (audiodata[c] != nullptr) {
      for (size_t i = 0; i < audio->frames; i++) {
        audiodata[c][i] = audiodata[0][i];
      }
    }
  }

  return audio;
}

void destroy_web_pitch_filter(void *data) {
  // NOTE: weird mix of C++ and C here, because I'm using obs's allocator
  WebPitchFilter *filter = reinterpret_cast<WebPitchFilter *>(data);
  std::lock_guard<std::mutex> lock(filter->audio_mutex);
  filter->stop_thread_server();

  free(filter->rubberband.release());
  free(filter->app.release());
  free(filter->thread.release());
  bfree(filter);
}

static bool start_server(obs_properties_t *, obs_property_t *, void *data) {
  WebPitchFilter *filter = reinterpret_cast<WebPitchFilter *>(data);
  filter->start_thread_server();
  return false;
}

static bool end_server(obs_properties_t *, obs_property_t *, void *data) {
  WebPitchFilter *filter = reinterpret_cast<WebPitchFilter *>(data);
  filter->stop_thread_server();
  return false;
}

static obs_properties_t *web_pitch_properties(void *) {
  obs_properties_t *props = obs_properties_create();
  obs_properties_add_text(props, "web_addr", "Listen Address",
                          OBS_TEXT_DEFAULT);
  obs_properties_add_int(props, "web_port", "Web Port", 0, 65535, 1);
  return props;
}

static void web_pitch_defaults(obs_data_t *settings) {
  obs_data_set_default_string(settings, "web_addr", "0.0.0.0");
  obs_data_set_default_int(settings, "web_port", 8085);
}

void *create_web_pitch_filter(obs_data_t *settings, obs_source_t *) {
  // TODO: It'll be a good idea to figure out sample rate and channel here, but
  // I also don't want to import obs-studio's internal headers, so I'll just
  // guess them here.
  const int sample_rate = 48000;
  const int channels = 2;
  auto port = (uint16_t)obs_data_get_int(settings, "web_port");
  auto addr = obs_data_get_string(settings, "web_addr");

  // a little roundabout, but we'll initialize a version of the filter here,
  // then move it into OBS's memory management
  WebPitchFilter *filter =
      new WebPitchFilter(sample_rate, channels, port, addr);
  WebPitchFilter *data =
      reinterpret_cast<WebPitchFilter *>(bzalloc(sizeof(WebPitchFilter)));
  data->rubberband = std::move(filter->rubberband);
  data->app = std::move(filter->app);
  data->addr = filter->addr;
  data->port = filter->port;

  data->start_thread_server();
  free(filter);
  obs_log(LOG_INFO, "Address of data is: %p", data);

  return data;
}

struct obs_source_info pitch_shift_filter = {
    .id = "pitch_shift_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name = [](void *) { return "Pitch Shift Filter"; },
    .create = create_web_pitch_filter,
    .destroy = destroy_web_pitch_filter,
    .get_defaults = web_pitch_defaults,
    .get_properties = web_pitch_properties,
    .filter_audio = process_audio,
};

bool obs_module_load(void) {
  obs_register_source(&pitch_shift_filter);
  obs_log(LOG_INFO, "Pitch Shift Filter loaded successfully (version %s)",
          PLUGIN_VERSION);
  return true;
}

void obs_module_unload(void) { obs_log(LOG_INFO, "plugin unloaded"); }
