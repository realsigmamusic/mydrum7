// https://github.com/realsigmamusic/mydrum7

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <sndfile.h>
#include <fstream>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cstdint>

#define MYDRUM7_URI "http://realsigmamusic.com/plugins/mydrum7"
#define NUM_OUTPUTS 12
#define MAX_VOICES 128 // 8 16 32 64 128

// Port indices
enum {
	PORT_MIDI_IN = 0,
	PORT_KICK_IN = 1,
	PORT_KICK_OUT = 2,
	PORT_SNARE_TOP = 3,
	PORT_SNARE_BOTTOM = 4,
	PORT_HIHAT = 5,
	PORT_RACKTOM1 = 6,
	PORT_RACKTOM2 = 7,
	PORT_RACKTOM3 = 8,
	PORT_FLOORTOM1 = 9,
	PORT_FLOORTOM2 = 10,
	PORT_OVERHEAD_L = 11,
	PORT_OVERHEAD_R = 12
};

// Leitor de .pak
struct PakReader {
	std::ifstream file;
	std::map<std::string, std::pair<uint64_t, uint64_t>> index; // path -> (offset, size)
	
	bool open(const std::string& pakPath) {
		file.open(pakPath, std::ios::binary);
		if (!file) return false;
		
		uint32_t count;
		file.read((char*)&count, sizeof(count));
		
		for (uint32_t i = 0; i < count; ++i) {
			uint32_t len;
			file.read((char*)&len, sizeof(len));
			
			std::string path(len, '\0');
			file.read(&path[0], len);
			
			uint64_t offset, size;
			file.read((char*)&offset, sizeof(offset));
			file.read((char*)&size, sizeof(size));
			
			index[path] = {offset, size};
		}
		
		fprintf(stderr, "[My Drum 7 LV2] PakReader Carregado: %u arquivos\n", count);
		return true;
	}
	
	std::vector<char> read(const std::string& path) {
		auto it = index.find(path);
		if (it == index.end()) return {};
		
		file.seekg(it->second.first);
		std::vector<char> data(it->second.second);
		file.read(data.data(), data.size());
		return data;
	}
};

struct Sample {
	std::vector<float> dataL;
	std::vector<float> dataR;
	int channels;
	int sampleRate;
	bool is_stereo;
	Sample() : channels(0), sampleRate(0), is_stereo(false) {}
};

struct VelocityLayer {
	uint8_t min_vel;
	uint8_t max_vel;
	std::vector<Sample> samples;
	uint32_t current_rr = 0;
	
	const Sample* getNextSample() {
		if (samples.empty()) return nullptr;
		const Sample* s = &samples[current_rr];
		current_rr = (current_rr + 1) % samples.size();
		return s;
	}
};

struct RRGroup {
	std::vector<VelocityLayer> velocity_layers;
	int output = 0;
	int chokeGroup = 0;
	
	const Sample* getSampleForVelocity(uint8_t velocity) {
		for (auto& layer : velocity_layers) {
			if (velocity >= layer.min_vel && velocity <= layer.max_vel) {
				return layer.getNextSample();
			}
		}
		if (!velocity_layers.empty()) {
			return velocity_layers[0].getNextSample();
		}
		return nullptr;
	}
};

struct Voice {
	const Sample* sample = nullptr;
	uint32_t pos = 0;
	uint32_t length = 0;
	int output = 0;
	float velocity = 1.0f;
	int chokeGroup = 0;
};

typedef struct {
	LV2_URID atom_Sequence;
	LV2_URID midi_MidiEvent;
} MyDrumKitURIs;

struct MyDrumKit {
	std::map<int, std::vector<RRGroup>> rr_groups;
	std::vector<Voice> voices;
	float* ports[16]; // 1 MIDI + 15 audio outputs
	double sample_rate = 44100.0;
	PakReader pak;
	MyDrumKitURIs uris;
	LV2_URID_Map* map;
	std::string bundle_path;

	MyDrumKit() {
		for (int i = 0; i < 16; ++i) ports[i] = nullptr;
		voices.reserve(MAX_VOICES);
	}

	static Sample load_wav(const char* data, size_t size, bool force_stereo = false) {
		SF_INFO info{};
		SF_VIRTUAL_IO vio;
		
		struct MemFile {
			const char* data;
			size_t size;
			size_t pos;
		} memfile = {data, size, 0};
	
		vio.get_filelen = [](void* user_data) -> sf_count_t {
			return ((MemFile*)user_data)->size;
		};
		vio.seek = [](sf_count_t offset, int whence, void* user_data) -> sf_count_t {
			MemFile* mf = (MemFile*)user_data;
			switch (whence) {
				case SEEK_SET: mf->pos = offset; break;
				case SEEK_CUR: mf->pos += offset; break;
				case SEEK_END: mf->pos = mf->size + offset; break;
			}
			if (mf->pos > mf->size) mf->pos = mf->size;
			return mf->pos;
		};
		vio.read = [](void* ptr, sf_count_t count, void* user_data) -> sf_count_t {
			MemFile* mf = (MemFile*)user_data;
			sf_count_t to_read = std::min((sf_count_t)(mf->size - mf->pos), count);
			memcpy(ptr, mf->data + mf->pos, to_read);
			mf->pos += to_read;
			return to_read;
		};
		vio.write = [](const void*, sf_count_t, void*) -> sf_count_t { return 0; };
		vio.tell = [](void* user_data) -> sf_count_t {
			return ((MemFile*)user_data)->pos;
		};
	
		SNDFILE* file = sf_open_virtual(&vio, SFM_READ, &info, &memfile);
		Sample s;
		if (!file) return s;
	
		s.channels = info.channels;
		s.sampleRate = info.samplerate;
		sf_count_t frames = info.frames;
		if (frames <= 0 || info.channels <= 0) {
			sf_close(file);
			return s;
		}
	
		std::vector<float> tmp(frames * info.channels);
		sf_read_float(file, tmp.data(), tmp.size());
		sf_close(file);
	
		if (force_stereo && info.channels >= 2) {
			s.is_stereo = true;
			s.dataL.resize(frames);
			s.dataR.resize(frames);
			for (sf_count_t i = 0; i < frames; ++i) {
				s.dataL[i] = tmp[i * info.channels + 0];
				s.dataR[i] = tmp[i * info.channels + 1];
			}
			s.channels = 2;
		} else if (info.channels == 1) {
			s.is_stereo = false;
			s.dataL = std::move(tmp);
			s.channels = 1;
		} else {
			s.is_stereo = false;
			s.dataL.resize(frames);
			for (sf_count_t i = 0; i < frames; ++i) {
				float sum = 0.0f;
				for (int c = 0; c < info.channels; ++c) sum += tmp[i * info.channels + c];
				s.dataL[i] = sum / (float)info.channels;
			}
			s.channels = 1;
		}
		return s;
	}

	void add_to_rr_group_with_velocity(int note, const char* relpath, int output, uint8_t min_vel, uint8_t max_vel, bool stereo = false) {
		auto data = pak.read(relpath);
		if (data.empty()) {
			fprintf(stderr, "[My Drum 7 LV2] Sample não encontrado: %s\n", relpath);
			return;
		}

		Sample s = load_wav(data.data(), data.size(), stereo);
		if (s.dataL.empty()) return;

		auto& noteGroups = rr_groups[note];
		
		auto group_it = std::find_if(noteGroups.begin(), noteGroups.end(),[&](const RRGroup& g){ return g.output == output; });
		
		if (group_it == noteGroups.end()) {
			RRGroup g;
			g.output = output;
			
			VelocityLayer layer;
			layer.min_vel = min_vel;
			layer.max_vel = max_vel;
			layer.samples.push_back(std::move(s));
			
			g.velocity_layers.push_back(std::move(layer));
			noteGroups.push_back(std::move(g));
		} else {
			auto layer_it = std::find_if(group_it->velocity_layers.begin(),group_it->velocity_layers.end(),[&](const VelocityLayer& l) {
				return l.min_vel == min_vel && l.max_vel == max_vel;
			});
			
			if (layer_it == group_it->velocity_layers.end()) {
				VelocityLayer layer;
				layer.min_vel = min_vel;
				layer.max_vel = max_vel;
				layer.samples.push_back(std::move(s));
				group_it->velocity_layers.push_back(std::move(layer));
			} else {
				layer_it->samples.push_back(std::move(s));
			}
		}
	}

	void load_instrument_samples(int midi_note, const std::string& base_name, int output, int num_velocity_layers, int num_rr, bool stereo = false) {
		int range = 127 / num_velocity_layers;
		for (int vel_layer = 1; vel_layer <= num_velocity_layers; ++vel_layer) {
			uint8_t min_vel = (vel_layer - 1) * range + 1;
			uint8_t max_vel = (vel_layer == num_velocity_layers) ? 127 : vel_layer * range;
		
			for (int rr = 1; rr <= num_rr; ++rr) {
				char vel_str[8], rr_str[8];
				snprintf(vel_str, sizeof(vel_str), "%02d", vel_layer);
				snprintf(rr_str, sizeof(rr_str), "%01d", rr);
			
				std::string path = base_name + "_r" + std::string(rr_str) + "_v" + std::string(vel_str) + ".wav";
				add_to_rr_group_with_velocity(midi_note, path.c_str(), output, min_vel, max_vel, stereo);
			}
		}
	}

	bool loadSamplesFromFolder() { // output, velocity layers, round robin
		try {
			// Kick
			load_instrument_samples(35, "mic1_kick", 0, 10, 2);
			load_instrument_samples(36, "mic1_kick", 0, 10, 2);
			load_instrument_samples(35, "mic2_kick", 1, 10, 2);
			load_instrument_samples(36, "mic2_kick", 1, 10, 2);
			load_instrument_samples(35, "mic3_kick", 3, 10, 2);
			load_instrument_samples(36, "mic3_kick", 3, 10, 2);
			load_instrument_samples(35, "ovhd_kick", 10, 10, 2, true);
			load_instrument_samples(36, "ovhd_kick", 10, 10, 2, true);

			// Snare Sidestick
			load_instrument_samples(37, "mic1_snare_sidestick", 2, 9, 2);
			load_instrument_samples(37, "mic2_snare_sidestick", 3, 9, 2);
			load_instrument_samples(37, "ovhd_snare_sidestick", 10, 9, 2, true);

			// Snare Center
			load_instrument_samples(38, "mic1_snare_center", 2, 13, 4);
			load_instrument_samples(38, "mic2_snare_center", 3, 13, 4);
			load_instrument_samples(38, "ovhd_snare_center", 10, 13, 4, true);

			// Snare Edge
			load_instrument_samples(39, "mic1_snare_edge", 2, 12, 4);
			load_instrument_samples(39, "mic2_snare_edge", 3, 12, 4);
			load_instrument_samples(39, "ovhd_snare_edge", 10, 12, 4, true);

			// Snare Rimshot
			load_instrument_samples(40, "mic1_snare_rimshot", 2, 9, 2);
			load_instrument_samples(40, "mic2_snare_rimshot", 3, 9, 2);
			load_instrument_samples(40, "ovhd_snare_rimshot", 10, 9, 2, true);

			// Hihat Edge
			load_instrument_samples(29, "mic1_hihat_edge_closed", 4, 10, 4);
			load_instrument_samples(29, "ovhd_hihat_edge_closed", 10, 10, 4, true);

			load_instrument_samples(30, "mic1_hihat_edge_open1", 4, 10, 2);
			load_instrument_samples(30, "ovhd_hihat_edge_open1", 10, 10, 2, true);
			load_instrument_samples(31, "mic1_hihat_edge_open2", 4, 10, 2);
			load_instrument_samples(31, "ovhd_hihat_edge_open2", 10, 10, 2, true);
			load_instrument_samples(32, "mic1_hihat_edge_open3", 4, 10, 2);
			load_instrument_samples(32, "ovhd_hihat_edge_open3", 10, 10, 2, true);
			load_instrument_samples(33, "mic1_hihat_edge_open4", 4, 10, 2);
			load_instrument_samples(33, "ovhd_hihat_edge_open4", 10, 10, 2, true);

			load_instrument_samples(34, "mic1_hihat_edge_open_full", 4, 9, 2);
			load_instrument_samples(34, "ovhd_hihat_edge_open_full", 10, 9, 2, true);

			// Hihat Pedal
			load_instrument_samples(44, "mic1_hihat_pedal_closed", 4, 8, 2);
			load_instrument_samples(44, "ovhd_hihat_pedal_closed", 10, 8, 2, true);

			load_instrument_samples(23, "mic1_hihat_pedal_open", 4, 5, 2);
			load_instrument_samples(23, "ovhd_hihat_pedal_open", 10, 5, 2, true);

			// Hihat Bow
			load_instrument_samples(42, "mic1_hihat_bow_closed", 4, 9, 4);
			load_instrument_samples(42, "ovhd_hihat_bow_closed", 10, 9, 4, true);

			load_instrument_samples(24, "mic1_hihat_bow_closed", 4, 9, 4);
			load_instrument_samples(24, "ovhd_hihat_bow_closed", 10, 9, 4, true);
			load_instrument_samples(25, "mic1_hihat_bow_open1", 4, 10, 2);
			load_instrument_samples(25, "ovhd_hihat_bow_open1", 10, 10, 2, true);
			load_instrument_samples(26, "mic1_hihat_bow_open2", 4, 10, 2);
			load_instrument_samples(26, "ovhd_hihat_bow_open2", 10, 10, 2, true);
			load_instrument_samples(27, "mic1_hihat_bow_open3", 4, 10, 2);
			load_instrument_samples(27, "ovhd_hihat_bow_open3", 10, 10, 2, true);
			load_instrument_samples(28, "mic1_hihat_bow_open4", 4, 10, 2);
			load_instrument_samples(28, "ovhd_hihat_bow_open4", 10, 10, 2, true);

			load_instrument_samples(46, "mic1_hihat_bow_open_full", 4, 10, 2);
			load_instrument_samples(46, "ovhd_hihat_bow_open_full", 10, 10, 2, true);

			// Choke groups (HiHat)
			for (auto& g : rr_groups[23]) g.chokeGroup = 1;
			for (auto& g : rr_groups[24]) g.chokeGroup = 1;
			for (auto& g : rr_groups[25]) g.chokeGroup = 1;
			for (auto& g : rr_groups[26]) g.chokeGroup = 1;
			for (auto& g : rr_groups[27]) g.chokeGroup = 1;
			for (auto& g : rr_groups[28]) g.chokeGroup = 1;
			for (auto& g : rr_groups[29]) g.chokeGroup = 1;
			for (auto& g : rr_groups[30]) g.chokeGroup = 1;
			for (auto& g : rr_groups[31]) g.chokeGroup = 1;
			for (auto& g : rr_groups[32]) g.chokeGroup = 1;
			for (auto& g : rr_groups[33]) g.chokeGroup = 1;
			for (auto& g : rr_groups[34]) g.chokeGroup = 1;
			for (auto& g : rr_groups[42]) g.chokeGroup = 1;
			for (auto& g : rr_groups[44]) g.chokeGroup = 1;
			for (auto& g : rr_groups[46]) g.chokeGroup = 1;

			// Tom 1
			load_instrument_samples(48, "mic1_tom1", 5, 10, 2);
			load_instrument_samples(48, "mic2_tom1", 3, 10, 2);
			load_instrument_samples(48, "ovhd_tom1", 10, 10, 2, true);

			// Tom 2
			load_instrument_samples(47, "mic1_tom2", 6, 12, 2);
			load_instrument_samples(47, "mic2_tom2", 3, 12, 2);
			load_instrument_samples(47, "ovhd_tom2", 10, 12, 2, true);

			// Tom 3
			load_instrument_samples(45, "mic1_tom3", 7, 9, 2);
			load_instrument_samples(45, "mic2_tom3", 3, 9, 2);
			load_instrument_samples(45, "ovhd_tom3", 10, 9, 2, true);

			// Tom 4
			load_instrument_samples(43, "mic1_tom4", 8, 9, 2);
			load_instrument_samples(43, "mic2_tom4", 3, 9, 2);
			load_instrument_samples(43, "ovhd_tom4", 10, 9, 2, true);

			// Tom 5
			load_instrument_samples(41, "mic1_tom5", 9, 9, 2);
			load_instrument_samples(41, "mic2_tom5", 3, 9, 2);
			load_instrument_samples(41, "ovhd_tom5", 10, 9, 2, true);

			// Ride
			load_instrument_samples(51, "ovhd_ride_bow", 10, 8, 4, true);
			load_instrument_samples(53, "ovhd_ride_bell", 10, 7, 2, true);
			load_instrument_samples(59, "ovhd_ride_edge", 10, 9, 2, true);

			// Crash 1
			load_instrument_samples(49, "ovhd_crash1_bow", 10, 14, 2, true);
			load_instrument_samples(52, "ovhd_crash1_edge", 10, 14, 2, true);

			// Crash 2
			load_instrument_samples(57, "ovhd_crash2_edge", 10, 11, 2, true);
			load_instrument_samples(55, "ovhd_crash2_bow", 10, 11, 2, true);

			fprintf(stderr, "[My Drum 7 LV2] %zu notas carregadas\n", rr_groups.size());
		} catch (...) {
			fprintf(stderr, "[My Drum 7 LV2] Erro ao carregar samples\n");
			return false;
		}
		return true;
	}

	void run_render(uint32_t n_samples) {
		// Limpa outputs de áudio
		for (int i = 1; i < 16; ++i) {
			if (ports[i])
				std::memset(ports[i], 0, sizeof(float) * n_samples);
		}

		for (size_t voice_idx = 0; voice_idx < voices.size(); ) {
			Voice& v = voices[voice_idx];
			if (!v.sample || v.sample->dataL.empty() || v.pos >= v.length) {
				// Swap and Pop para remoção eficiente O(1)
				v = voices.back();
				voices.pop_back();
				continue;
			}
			
			const float* dataL = v.sample->dataL.data();
			const float* dataR = v.sample->is_stereo ? v.sample->dataR.data() : nullptr;
			
			int audio_port_L = v.output + 1;
			int audio_port_R = v.output + 2;
			
			for (uint32_t i = 0; i < n_samples && v.pos < v.length; ++i) {
				if (audio_port_L < 16 && ports[audio_port_L]) {
					ports[audio_port_L][i] += dataL[v.pos] * v.velocity;
				}
				if (dataR && audio_port_R < 16 && ports[audio_port_R]) {
					ports[audio_port_R][i] += dataR[v.pos] * v.velocity;
				}
				v.pos++;
			}
			
			if (v.pos >= v.length) {
				v = voices.back();
				voices.pop_back();
			} else {
				voice_idx++;
			}
		}
	}
};

// LV2 Callbacks
static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
	MyDrumKit* self = new MyDrumKit();
	self->sample_rate = rate;
	self->bundle_path = bundle_path;
	
	// Map URIDs
	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID__map)) {
			self->map = (LV2_URID_Map*)features[i]->data;
		}
	}
	
	if (!self->map) {
		fprintf(stderr, "[My Drum 7 LV2] ERRO: Host não forneceu LV2_URID_Map\n");
		delete self;
		return nullptr;
	}
	
	self->uris.atom_Sequence = self->map->map(self->map->handle, LV2_ATOM__Sequence);
	self->uris.midi_MidiEvent = self->map->map(self->map->handle, LV2_MIDI__MidiEvent);
	
	fprintf(stderr, "[My Drum 7 LV2] Instantiate SR=%.1f, bundle=%s\n", rate, bundle_path);
	return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
	MyDrumKit* self = (MyDrumKit*)instance;
	if (port < 16) {
		self->ports[port] = (float*)data;
	}
}

static void activate(LV2_Handle instance) {
	MyDrumKit* self = (MyDrumKit*)instance;
	
	if (self->rr_groups.empty()) {
		std::string bundle = self->bundle_path;
		if (!bundle.empty() && bundle.back() != '/') bundle += "/";
		std::string pak_path = bundle + "sounds.pak";
		fprintf(stderr, "[My Drum 7 LV2] Carregando pak: %s\n", pak_path.c_str());
		
		if (!self->pak.open(pak_path)) {
			fprintf(stderr, "[My Drum 7 LV2] ERRO: não conseguiu abrir %s\n", pak_path.c_str());
			return;
		}
		
		if (!self->loadSamplesFromFolder()) {
			fprintf(stderr, "[My Drum 7 LV2] AVISO: Erro ao carregar samples\n");
		}
	}
	
	fprintf(stderr, "[My Drum 7 LV2] Activate\n");
}

static void run(LV2_Handle instance, uint32_t n_samples) {
	MyDrumKit* self = (MyDrumKit*)instance;
	if (!self) return;
	
	// Processar eventos MIDI da porta 0
	const LV2_Atom_Sequence* seq = (const LV2_Atom_Sequence*)self->ports[PORT_MIDI_IN];
	
	if (seq) {
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
			if (ev->body.type == self->uris.midi_MidiEvent) {
				const uint8_t* const msg = (const uint8_t*)(ev + 1);
				uint8_t status = msg[0] & 0xF0;
				
				if (status == 0x90 && msg[2] > 0) {  // Note ON
					uint8_t note = msg[1];
					uint8_t vel = msg[2];
					
					auto it = self->rr_groups.find(note);
					if (it != self->rr_groups.end()) {
						// Coleta chokeGroups
						std::vector<int> chokes;
						for (RRGroup& group : it->second) {
							if (group.chokeGroup > 0)
								chokes.push_back(group.chokeGroup);
						}

						// Aplica choke
						if (!chokes.empty()) {
							self->voices.erase(
								std::remove_if(self->voices.begin(), self->voices.end(),
									[&](const Voice& v){
										return std::find(chokes.begin(), chokes.end(), v.chokeGroup) != chokes.end();
									}),
								self->voices.end());
						}

						// Dispara samples
						for (RRGroup& group : it->second) {
							const Sample* sample = group.getSampleForVelocity(vel);
							if (!sample || sample->dataL.empty()) continue;

							Voice v;
							v.sample = sample;
							v.pos = 0;
							v.length = (uint32_t)sample->dataL.size();
							v.output = group.output;
							v.chokeGroup = group.chokeGroup;
							// v.velocity = 1.0f;
							float norm = (float)vel / 127.0f;
							v.velocity = 0.4f + (0.6f * norm); // (60% fixo, 40% variável).
							// v.velocity = 0.2f + (0.8f * norm); (20% fixo, 80% variável).
							
							// Voice Stealing Inteligente: Rouba a voz que já tocou por mais tempo (maior pos)
							if (self->voices.size() >= MAX_VOICES) {
								size_t oldest_idx = 0;
								uint32_t max_pos = 0;
								for (size_t i = 0; i < self->voices.size(); ++i) {
									if (self->voices[i].pos > max_pos) {
										max_pos = self->voices[i].pos;
										oldest_idx = i;
									}
								}
								self->voices[oldest_idx] = v;
							} else {
								self->voices.push_back(v);
							}
						}
					}
				}
			}
		}
	}
	
	// Renderizar áudio
	self->run_render(n_samples);
}

static void deactivate(LV2_Handle instance) {
	fprintf(stderr, "[My Drum 7 LV2] Deactivate\n");
}

static void cleanup(LV2_Handle instance) {
	MyDrumKit* self = (MyDrumKit*)instance;
	fprintf(stderr, "[My Drum 7 LV2] Cleanup\n");
	if (self) delete self;
}

static const void* extension_data(const char* uri) {
	return nullptr;
}

static const LV2_Descriptor descriptor = {
	MYDRUM7_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data
};

extern "C" {
	LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
		return (index == 0) ? &descriptor : nullptr;
	}
}
