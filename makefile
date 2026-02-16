CXXFLAGS += -fPIC -O2 -std=c++17
LDFLAGS += -shared -lsndfile

# Flags para a interface gráfica (UI)
UI_FLAGS = $(shell pkg-config --cflags --libs cairo x11)

PLUGIN = mydrum7
PAK = sounds.pak
BUNDLE = $(PLUGIN).lv2

all: dsp ui
	@echo "Plugin completo compilado: $(BUNDLE)"

dsp:
	$(CXX) -o $(PLUGIN).so main.cpp $(CXXFLAGS) $(LDFLAGS)

ui:
	# Compila a interface visual linkando Cairo e X11
	$(CXX) -o ui.so ui.cpp $(CXXFLAGS) -shared -fPIC $(UI_FLAGS)

pak:
	@mkdir -p build
	$(CXX) -std=c++17 -O2 tools/soundmaker.cpp -o build/soundmaker
	./build/soundmaker samples $(PAK)

install:
	mkdir -p ~/.lv2/$(BUNDLE)
	cp $(PLUGIN).so ~/.lv2/$(BUNDLE)/
	cp ui.so ~/.lv2/$(BUNDLE)/
	cp manifest.ttl ~/.lv2/$(BUNDLE)/
	cp mydrum7.ttl ~/.lv2/$(BUNDLE)/
	cp wallpaper.png ~/.lv2/$(BUNDLE)/
	cp $(PAK) ~/.lv2/$(BUNDLE)/
	@echo "Instalado em ~/.lv2/$(BUNDLE)/"

clean:
	rm -f *.so $(PAK)

uninstall:
	rm -rf ~/.lv2/$(BUNDLE)