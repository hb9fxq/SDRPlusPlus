#include "spectran_http_client.h"
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <gui/widgets/stepped_slider.h>
#include <utils/optionlist.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "spectran_http_source",
    /* Description:     */ "Spectran V6 HTTP source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const uint64_t sampleRates[] = {
    100000,
    250000,
    500000,
    1000000,
    1500000,
    2000000,
    3000000,
    4000000,
    5000000,
    6000000,
    10000000
};

const char* sampleRatesTxt[] = {
    "100KHz",
    "250KHz",
    "500KHz",
    "1MHz",
    "1.5MHz",
    "2MHz",
    "3MHz",
    "4MHz",
    "5MHz",
    "6MHz",
    "10MHz"
};

class SpectranHTTPSourceModule : public ModuleManager::Instance {
public:
    SpectranHTTPSourceModule(std::string name) {
        this->name = name;

        strcpy(hostname, "localhost");
        strcpy(demodulatorBlockApiName, "Block_IQDemodulator_0");

        sampleRate = 100000;

        // Load config
        config.acquire();
        
        if (config.conf[name].contains("hostname")) {
            std::string hostStr = config.conf[name]["hostname"] ;
            strcpy(hostname, hostStr.c_str());
        }

        if (config.conf[name].contains("demodulatorBlockApiName")) {
            std::string demodulatorBlockApiNameStr = config.conf[name]["demodulatorBlockApiName"] ;
            strcpy(demodulatorBlockApiName, demodulatorBlockApiNameStr.c_str());
        }

        if (config.conf[name].contains("port")) {
            port = config.conf[name]["port"];
            port = std::clamp<int>(port, 1, 65535);
        }

        if (config.conf[name].contains("sampleRate")) {
            int sr = config.conf[name]["sampleRate"];
            sampleRate = sampleRates[sr]; 
            srId = sr;
        }
       
        config.release();

        sampleRateRequested  = sampleRate;

        for (int i = 0; i < 11; i++) {
            sampleRateListTxt += sampleRatesTxt[i];
            sampleRateListTxt += '\0';
        }

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        sigpath::sourceManager.registerSource("Spectran HTTP", &handler);
    }

    ~SpectranHTTPSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Spectran HTTP");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    // TODO: Implement select functions

private:
    static void menuSelected(void* ctx) {
        SpectranHTTPSourceModule* _this = (SpectranHTTPSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("SpectranHTTPSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        SpectranHTTPSourceModule* _this = (SpectranHTTPSourceModule*)ctx;
        gui::mainWindow.playButtonLocked = false;
        flog::info("SpectranHTTPSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        SpectranHTTPSourceModule* _this = (SpectranHTTPSourceModule*)ctx;
        bool connected = (_this->client && _this->client->isOpen());
        if (_this->running && connected) { return; }

        // TODO: Start
        _this->client->streaming(true);

        // TODO: Set options

        _this->running = true;
        flog::info("SpectranHTTPSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        SpectranHTTPSourceModule* _this = (SpectranHTTPSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        
        // TODO: Implement stop
        _this->client->streaming(false);

        flog::info("SpectranHTTPSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        SpectranHTTPSourceModule* _this = (SpectranHTTPSourceModule*)ctx;
        bool connected = (_this->client && _this->client->isOpen());
        if (connected) {
            int64_t newfreq = round(freq);
            if (newfreq != _this->lastReportedFreq /*&& _this->gotReport*/) {
                flog::debug("Sending tuning command");
                _this->lastReportedFreq = newfreq;
                _this->client->setCenterFrequency(newfreq);
            }
        }
        _this->freq = freq;
        flog::info("SpectranHTTPSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        SpectranHTTPSourceModule* _this = (SpectranHTTPSourceModule*)ctx;
        bool connected = (_this->client && _this->client->isOpen());
        gui::mainWindow.playButtonLocked = !connected;

        if (connected) { SmGui::BeginDisabled(); }

        ImGui::LeftLabel("Rate");
        if (SmGui::Combo(CONCAT("##spectran_sr_sel_", _this->name), &_this->srId, _this->sampleRateListTxt.c_str())) {
            flog::debug("Setting requested sample rate: {}", sampleRates[_this->srId]);
            _this->sampleRateRequested = sampleRates[_this->srId];
            //core::setInputSampleRate(_this->sampleRate);          
            config.acquire();
            config.conf[_this->name]["sampleRate"] = _this->srId;
            config.release(true);
        }        
        
        ImGui::LeftLabel("Host");
        if (SmGui::InputText(CONCAT("##spectran_http_host_", _this->name), _this->hostname, 1023)) {
            config.acquire();
            config.conf[_this->name]["hostname"] = _this->hostname;
            config.release(true);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        ImGui::LeftLabel("Port");
        if (SmGui::InputInt(CONCAT("##spectran_http_port_", _this->name), &_this->port, 0, 0)) {
            config.acquire();
            config.conf[_this->name]["port"] = _this->port;
            config.release(true);
        }

        ImGui::LeftLabel("Demod Block");
        if (SmGui::InputText(CONCAT("##spectran_demodulator_block_api_name_", _this->name), _this->demodulatorBlockApiName, 1023)) {
            config.acquire();
            config.conf[_this->name]["demodulatorBlockApiName"] = _this->demodulatorBlockApiName;
            config.release(true);
        }


        if (connected) { SmGui::EndDisabled(); }

        if (_this->running) { style::beginDisabled(); }
        SmGui::FillWidth();
        if (!connected && SmGui::Button("Connect##spectran_http_source")) {
            _this->tryConnect();
        }
        else if (connected && SmGui::Button("Disconnect##spectran_http_source")) {
            _this->client->onCenterFrequencyChanged.unbind(_this->onFreqChangedId);
            _this->client->onSamplerateChanged.unbind(_this->onSamplerateChangedId);
            _this->client->close();
        }
        if (_this->running) { style::endDisabled(); }

        SmGui::Text("Status:");
        SmGui::SameLine();
        if (connected) {
            SmGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
        }
        else {
            SmGui::Text("Not connected");
        }
    }

    void tryConnect() {
        try {
            gotReport = false;
            client = std::make_shared<SpectranHTTPClient>(hostname, port, &stream, sampleRateRequested, demodulatorBlockApiName);
            onFreqChangedId = client->onCenterFrequencyChanged.bind(&SpectranHTTPSourceModule::onFreqChanged, this);
            onSamplerateChangedId = client->onSamplerateChanged.bind(&SpectranHTTPSourceModule::onSamplerateChanged, this);
            client->startWorker();
            client->setCenterFrequency(round(freq));


        }
        catch (std::runtime_error e) {
            flog::error("Could not connect: {0}", e.what());
        }
    }

    void onFreqChanged(double newFreq) {
        if (lastReportedFreq == newFreq) { return; }
        lastReportedFreq = newFreq;
        tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", newFreq);
        gotReport = true;
    }

    void onSamplerateChanged(double newSr) {
        core::setInputSampleRate(newSr);
    }

    std::string name;
    int srId = 0;
    bool enabled = true;
    uint64_t sampleRate;
    uint64_t sampleRateRequested;
    SourceManager::SourceHandler handler;
    bool running = false;

    std::shared_ptr<SpectranHTTPClient> client;
    HandlerID onFreqChangedId;
    HandlerID onSamplerateChangedId;

    double freq;

    int64_t lastReportedFreq = 0;
    bool gotReport;

    char hostname[1024];
    char demodulatorBlockApiName[1024];
    int port = 54664;
    dsp::stream<dsp::complex_t> stream;

     std::string sampleRateListTxt;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/spectran_http_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SpectranHTTPSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (SpectranHTTPSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}