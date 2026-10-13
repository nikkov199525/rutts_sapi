#include <windows.h>
#include "RuTtsSapiDdk.h"
#include "Guid.h"
#include "ParamReader.h"
#include "RulexClient.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)

struct Site final : ISpTTSEngineSite {
    // Audio is kept as raw bytes: the tests ask for both 8- and 16-bit formats.
    std::vector<uint8_t> audio;
    long rate=0;
    USHORT volume=100;
    bool abort=false;
    size_t abortAfter=0;
    int bookmarks=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p) override {*p=this;return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return 1;}
    ULONG STDMETHODCALLTYPE Release() override {return 1;}
    HRESULT STDMETHODCALLTYPE AddEvents(const SPEVENT* events,ULONG count) override {
        for(ULONG i=0;i<count;++i) if(events[i].eEventId==SPEI_TTS_BOOKMARK) ++bookmarks;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEventInterest(ULONGLONG* p) override {*p=~0ULL;return S_OK;}
    DWORD STDMETHODCALLTYPE GetActions() override {return abort || (abortAfter && audio.size()>=abortAfter)?SPVES_ABORT:0;}
    HRESULT STDMETHODCALLTYPE Write(const void* b,ULONG size,ULONG* written) override {
        CHECK(size<=4096);
        const uint8_t* pcm=static_cast<const uint8_t*>(b);
        audio.insert(audio.end(),pcm,pcm+size);*written=size;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRate(long* p) override {*p=rate;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetVolume(USHORT* p) override {*p=volume;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSkipInfo(SPVSKIPTYPE*,long*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CompleteSkip(long) override {return S_OK;}
};

// rutts.ini is UTF-16LE with a BOM, as the engine and the configurator write it:
// GetPrivateProfile* would read a UTF-8 file in the ANSI code page.
static void Write(const std::filesystem::path& p,const std::string& utf8) {
    const int n=MultiByteToWideChar(CP_UTF8,0,utf8.data(),(int)utf8.size(),nullptr,0);
    std::wstring w(static_cast<size_t>(n),L' ');
    MultiByteToWideChar(CP_UTF8,0,utf8.data(),(int)utf8.size(),w.data(),n);
    std::ofstream f(p,std::ios::binary);
    const wchar_t bom=0xFEFF;f.write(reinterpret_cast<const char*>(&bom),sizeof(bom));f.write(reinterpret_cast<const char*>(w.data()),(std::streamsize)(w.size()*sizeof(wchar_t)));
    CHECK(f.good());
}

int main(int argc,char** argv) {
    CHECK(argc==2);
    auto bin=std::filesystem::absolute(argv[1]);
    auto config=bin.parent_path()/"test-config";
    std::filesystem::create_directories(config);
    SetEnvironmentVariableW(L"RUTTS_CONFIG_DIR",config.c_str());
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try {
        // Settings: defaults are written on first use, invalid values are clamped.
        std::filesystem::remove(config/"rutts.ini");
        auto p=ParamReader::Load();
        CHECK(std::filesystem::exists(config/"rutts.ini") && p.use_rulex && p.samples_per_sec==10000 && p.output_sample_rate==10000);
        Write(config/"rutts.ini","[Parameters]\nsamples_per_sec=99999\ninterpolation_multiplier=3\nuse_rulex=False\n");
        p=ParamReader::Load();CHECK(p.samples_per_sec==16000 && p.interpolation_multiplier==4 && !p.use_rulex);

        // Rulex is found next to the build and accents words.
        std::wstring accented;
        {
            RulexClient rulex;rulex.EnsureLoaded(bin.wstring());CHECK(rulex.HasRulex());
            accented=rulex.ApplyToRussianWords(L"молоко");CHECK(accented!=L"молоко" && accented.find(L'+')!=std::wstring::npos);
            std::wcout<<L"Rulex loaded and applied accents\n";
        }
        {
            // Installed layout: one read-only rulex.db above the arch folder, as in
            // Program Files where users cannot write. LMDB used to open a second,
            // writable handle to the file and failed there: rulex looked "invisible".
            // The folder name is Cyrillic, as in a localized profile: rulex gets the
            // path in UTF-8 and opens it only through the access_utf8.c shim.
            auto root=bin.parent_path()/(L"рулекс-"+std::to_wstring(GetCurrentProcessId()));
            auto arch=root/"x64";auto db=root/"rulex.db";
            std::error_code ec;
            std::filesystem::create_directories(arch);
            for(auto name:{"rutts_rulex.dll","rulex.dll"}) std::filesystem::copy_file(bin/name,arch/name);
            std::filesystem::copy_file(bin/"rulex.db",db);CHECK(SetFileAttributesW(db.c_str(),FILE_ATTRIBUTE_READONLY));
            {RulexClient shared;shared.EnsureLoaded(arch.wstring());CHECK(shared.HasRulex());CHECK(shared.ApplyToRussianWords(L"молоко")==accented);}
            SetFileAttributesW(db.c_str(),FILE_ATTRIBUTE_NORMAL);
            std::filesystem::remove_all(root,ec);
            std::cout<<"Rulex opened a shared read-only database\n";
        }

        HMODULE dll=LoadLibraryW((bin/L"rutts_sapi.dll").c_str());CHECK(dll);
        auto getClass=reinterpret_cast<HRESULT (WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll,"DllGetClassObject"));CHECK(getClass);
        IClassFactory* factory=nullptr;CHECK(SUCCEEDED(getClass(CLSID_ruTTS_sapi,IID_IClassFactory,reinterpret_cast<void**>(&factory))));
        auto newEngine=[&]() {
            ISpTTSEngine* engine=nullptr;
            CHECK(SUCCEEDED(factory->CreateInstance(nullptr,__uuidof(ISpTTSEngine),reinterpret_cast<void**>(&engine))));
            return engine;
        };

        {
            Write(config/"rutts.ini","[Parameters]\nuse_rulex=False\n");
            ISpTTSEngine* engine=newEngine();
            GUID fmt;WAVEFORMATEX* wave=nullptr;CHECK(SUCCEEDED(engine->GetOutputFormat(nullptr,nullptr,&fmt,&wave)));
            CHECK(wave->nSamplesPerSec==10000 && wave->wBitsPerSample==16 && wave->nChannels==1 && wave->nBlockAlign==2);
            auto speak=[&](const std::wstring& text,long fragRate,ULONG fragVolume,long siteRate,USHORT siteVolume) {
                SPVTEXTFRAG frag{};frag.State.eAction=SPVA_Speak;frag.State.RateAdj=fragRate;frag.State.Volume=fragVolume;
                frag.pTextStart=text.c_str();frag.ulTextLen=(ULONG)text.size();
                Site site;site.rate=siteRate;site.volume=siteVolume;
                CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,&frag,&site)));return site.audio;
            };
            const std::wstring text=L"Проверка скорости речи, раз два три.";
            auto plain=speak(text,0,100,0,100);CHECK(plain.size()>1000);
            CHECK(speak(text,0,100,8,100).size()<plain.size() && speak(text,0,100,-8,100).size()>plain.size());
            // XML state of the fragments (JAWS sends <rate AbsSpeed>, <volume level>
            // with every phrase) combines with SetRate/SetVolume as SAPI requires:
            // rates add up, volumes multiply.
            CHECK(speak(text,5,100,0,100)==speak(text,0,100,5,100));
            CHECK(speak(text,5,100,0,100).size()<plain.size() && speak(text,-5,100,0,100).size()>plain.size());
            CHECK(speak(text,-5,100,5,100)==plain);
            CHECK(speak(text,0,30,0,100)==speak(text,0,100,0,30) && speak(text,0,30,0,100)!=plain);
            for(uint8_t byte:speak(text,0,100,0,0)) CHECK(byte==0);
            // '<' and '>' are real characters here (SAPI has parsed the markup):
            // they used to swallow the rest of the phrase as a "tag".
            CHECK(speak(L"если а < б то дальше идёт текст",0,100,0,100)==speak(L"если а б то дальше идёт текст",0,100,0,100));
            // Stop requests are honoured before and during synthesis.
            SPVTEXTFRAG frag{};frag.State.eAction=SPVA_Speak;frag.State.Volume=100;frag.pTextStart=text.c_str();frag.ulTextLen=(ULONG)text.size();
            Site stopped;stopped.abort=true;CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,&frag,&stopped)));CHECK(stopped.audio.empty());
            Site midstop;midstop.abortAfter=100;CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,&frag,&midstop)));CHECK(midstop.audio.size()<plain.size());
            // A raised word (JAWS: a capital) gets its own pitch.
            std::wstring first=L"Открыть ",second=L"Файл";
            SPVTEXTFRAG a{},b{};
            a.State.eAction=SPVA_Speak;a.State.Volume=100;a.pTextStart=first.c_str();a.ulTextLen=(ULONG)first.size();a.pNext=&b;
            b.State=a.State;b.State.PitchAdj.MiddleAdj=10;b.pTextStart=second.c_str();b.ulTextLen=(ULONG)second.size();b.ulTextSrcOffset=(ULONG)first.size();
            Site raised;CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,&a,&raised)));
            b.State.PitchAdj.MiddleAdj=0;
            Site flat;CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,&a,&flat)));
            CHECK(flat.audio==speak(first+second,0,100,0,100) && flat.audio!=raised.audio);
            std::cout<<"Rate, volume, pitch, '<' and abort OK: "<<plain.size()<<" bytes\n";
            CoTaskMemFree(wave);engine->Release();
        }
        {
            // [Characters] holds Latin combinations, not just single letters, and
            // the longest one wins: "ee" beats "e".
            auto say=[&](const std::string& ini,const std::wstring& text) {
                Write(config/"rutts.ini",ini);
                ISpTTSEngine* engine=newEngine();
                GUID fmt;WAVEFORMATEX* wave=nullptr;CHECK(SUCCEEDED(engine->GetOutputFormat(nullptr,nullptr,&fmt,&wave)));
                SPVTEXTFRAG frag{};frag.State.eAction=SPVA_Speak;frag.State.Volume=100;frag.pTextStart=text.c_str();frag.ulTextLen=(ULONG)text.size();
                Site site;CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,&frag,&site)));
                CoTaskMemFree(wave);engine->Release();
                return site.audio;
            };
            const std::string base="[Parameters]\nuse_rulex=False\n";
            auto combined=say(base+"[Characters]\ne = е\nsh = ш\nee = и\n",L"sheesh");
            CHECK(!combined.empty() && combined==say(base+"[Characters]\n",L"шиш"));
            // Rulex changes what is spoken.
            CHECK(say("[Parameters]\nuse_rulex=True\n",L"молоко")!=say(base,L"молоко"));
            std::cout<<"Latin combinations and rulex reach the core\n";
        }
        {
            // Without interpolation SAPI gets the core audio untouched and 8 bits
            // hold it exactly; the last 5 ms fade out instead of breaking off.
            Write(config/"rutts.ini","[Parameters]\nuse_rulex=False\n");
            ISpTTSEngine* engine=newEngine();
            std::wstring text=L"Проверка голоса, сегодня хорошая погода.";
            SPVTEXTFRAG frag{};frag.State.eAction=SPVA_Speak;frag.State.Volume=100;frag.pTextStart=text.c_str();frag.ulTextLen=(ULONG)text.size();
            auto speakAs=[&](int bits) {
                WAVEFORMATEX w{};w.wFormatTag=WAVE_FORMAT_PCM;w.nChannels=1;w.nSamplesPerSec=10000;
                w.wBitsPerSample=(WORD)bits;w.nBlockAlign=(WORD)(bits/8);w.nAvgBytesPerSec=10000*w.nBlockAlign;
                Site site;CHECK(SUCCEEDED(engine->Speak(0,SPDFID_WaveFormatEx,&w,&frag,&site)));return site.audio;
            };
            auto eight=speakAs(8),sixteen=speakAs(16);
            CHECK(!eight.empty() && sixteen.size()==eight.size()*2);
            const size_t fade=10000*5/1000;
            CHECK(eight.size()>fade);
            for(size_t i=0;i<eight.size();++i) {
                const int16_t sample=(int16_t)(sixteen[2*i]|(sixteen[2*i+1]<<8));
                if(i<eight.size()-fade) CHECK((int)eight[i]-128==(sample>>8) && (sample&0xFF)==0);
                else CHECK(std::abs((int)eight[i]-128-(sample+128)/256)<=1);
            }
            CHECK(sixteen[sixteen.size()-2]==0 && sixteen.back()==0 && eight.back()==128);
            std::cout<<"Core audio reaches SAPI unchanged, ending in a fade: "<<eight.size()<<" bytes\n";
            engine->Release();
        }
        {
            // Bookmarks (JAWS sets one per word) meet resampling: flushing the
            // resampler must not drop the audio held back for the events.
            std::wstring first=L"Проверка голоса. ",second=L"Привет, мир!",mark=L"1";
            SPVTEXTFRAG a{},b{},c{};
            a.State.eAction=SPVA_Speak;a.State.Volume=100;a.pTextStart=first.c_str();a.ulTextLen=(ULONG)first.size();a.pNext=&b;
            b.State.eAction=SPVA_Bookmark;b.pTextStart=mark.c_str();b.ulTextLen=(ULONG)mark.size();b.ulTextSrcOffset=(ULONG)first.size();b.pNext=&c;
            c.State.eAction=SPVA_Speak;c.State.Volume=100;c.pTextStart=second.c_str();c.ulTextLen=(ULONG)second.size();c.ulTextSrcOffset=(ULONG)first.size();
            std::wstring whole=first+second;
            SPVTEXTFRAG plain{};plain.State.eAction=SPVA_Speak;plain.State.Volume=100;plain.pTextStart=whole.c_str();plain.ulTextLen=(ULONG)whole.size();
            size_t lengths[2]{},marked2x=0;
            for(int marked=0;marked<3;++marked) {
                // Third pass: interpolation doubles the output rate, bookmarks stay.
                Write(config/"rutts.ini",marked<2?"[Parameters]\nuse_rulex=False\n":"[Parameters]\nuse_rulex=False\ninterpolation_multiplier=2\n");
                ISpTTSEngine* engine=newEngine();
                GUID fmt;WAVEFORMATEX* wave=nullptr;CHECK(SUCCEEDED(engine->GetOutputFormat(nullptr,nullptr,&fmt,&wave)));
                CHECK(wave->nSamplesPerSec==(marked<2?10000u:20000u) && wave->wBitsPerSample==16);
                Site site;CHECK(SUCCEEDED(engine->Speak(0,fmt,wave,marked?&a:&plain,&site)));
                CHECK(site.bookmarks==(marked?1:0));
                if(marked<2) lengths[marked]=site.audio.size(); else marked2x=site.audio.size();
                CoTaskMemFree(wave);engine->Release();
            }
            CHECK(lengths[0]>1000 && lengths[1]==lengths[0]);
            CHECK(marked2x>lengths[1]*19/10 && marked2x<lengths[1]*21/10);
            std::cout<<"Bookmarked audio survives resampling: "<<lengths[1]<<" -> "<<marked2x<<" bytes with interpolation\n";
        }
        factory->Release();
        auto canUnload=reinterpret_cast<HRESULT (WINAPI*)()>(GetProcAddress(dll,"DllCanUnloadNow"));
        CHECK(canUnload && canUnload()==S_OK);
        FreeLibrary(dll);
        CoUninitialize();
        std::cout<<"All integration checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';CoUninitialize();return 1;}
}
