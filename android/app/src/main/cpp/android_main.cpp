// Vega Sketch - Android audio (Oboe, low latency) and the JNI bridge to the Kotlin app.
#include <jni.h>
#include <oboe/Oboe.h>
#include <android/log.h>
#include <memory>
#include <vector>
#include <string>
#include "SketchEngine.h"
#include "Wav.h"

#define LOGI(...) __android_log_print (ANDROID_LOG_INFO, "VegaSketch", __VA_ARGS__)

namespace
{
vega::SketchEngine engine;

class Host : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback
{
public:
    bool start()
    {
        oboe::AudioStreamBuilder b;
        b.setDirection (oboe::Direction::Output)->setPerformanceMode (oboe::PerformanceMode::LowLatency)
         ->setSharingMode (oboe::SharingMode::Exclusive)->setFormat (oboe::AudioFormat::Float)->setChannelCount (2)
         ->setUsage (oboe::Usage::Game)->setDataCallback (this)->setErrorCallback (this);
        auto r = b.openStream (out);
        if (r != oboe::Result::OK) { LOGI ("output open failed: %s", oboe::convertToText (r)); return false; }
        engine.prepare (out->getSampleRate());
        out->setBufferSizeInFrames (out->getFramesPerBurst() * 2);
        return out->requestStart() == oboe::Result::OK;
    }
    void stopAll()
    {
        stopInput();
        if (out) { out->stop(); out->close(); out.reset(); }
    }
    bool startInput()
    {
        if (in) return true;
        oboe::AudioStreamBuilder b;
        b.setDirection (oboe::Direction::Input)->setPerformanceMode (oboe::PerformanceMode::LowLatency)
         ->setFormat (oboe::AudioFormat::Float)->setChannelCount (1)->setSampleRate (out ? out->getSampleRate() : 48000)
         ->setInputPreset (oboe::InputPreset::Unprocessed)
         ->setSampleRateConversionQuality (oboe::SampleRateConversionQuality::Medium)->setDataCallback (&inputCb);
        if (b.openStream (in) != oboe::Result::OK) { in.reset(); return false; }
        return in->requestStart() == oboe::Result::OK;
    }
    void stopInput() { if (in) { in->stop(); in->close(); in.reset(); } }
    int sampleRate() const { return out ? out->getSampleRate() : 48000; }

    oboe::DataCallbackResult onAudioReady (oboe::AudioStream*, void* data, int32_t frames) override
    {
        if ((int) l.size() < frames) { l.resize ((size_t) frames); r.resize ((size_t) frames); }
        engine.render (l.data(), r.data(), frames);
        auto* o = static_cast<float*> (data);
        for (int i = 0; i < frames; ++i) { o[2 * i] = l[(size_t) i]; o[2 * i + 1] = r[(size_t) i]; }
        return oboe::DataCallbackResult::Continue;
    }
    void onErrorAfterClose (oboe::AudioStream*, oboe::Result) override
    {   // headphones plugged / unplugged, Bluetooth, etc.: reopen
        out.reset();
        start();
        if (in) { in.reset(); startInput(); }
    }

private:
    struct InputCb : oboe::AudioStreamDataCallback
    {
        oboe::DataCallbackResult onAudioReady (oboe::AudioStream*, void* data, int32_t frames) override
        {
            engine.pushInput (static_cast<const float*> (data), frames);
            return oboe::DataCallbackResult::Continue;
        }
    } inputCb;
    std::shared_ptr<oboe::AudioStream> out, in;
    std::vector<float> l, r;
} host;

std::string str (JNIEnv* env, jstring s)
{
    const char* c = env->GetStringUTFChars (s, nullptr);
    std::string r (c != nullptr ? c : "");
    env->ReleaseStringUTFChars (s, c);
    return r;
}
}

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_vegaproductions_sketch_NativeEngine_##name

FN (jboolean, start) (JNIEnv*, jobject) { return host.start(); }
FN (void, shutdown) (JNIEnv*, jobject) { host.stopAll(); }
FN (jint, sampleRate) (JNIEnv*, jobject) { return host.sampleRate(); }
FN (jboolean, startInput) (JNIEnv*, jobject) { return host.startInput(); }
FN (void, stopInput) (JNIEnv*, jobject) { host.stopInput(); }
FN (void, setTempo) (JNIEnv*, jobject, jdouble bpm) { engine.setTempo (bpm); }
FN (void, setBeatsPerBar) (JNIEnv*, jobject, jint n) { engine.setBeatsPerBar (n); }
FN (void, setMetronome) (JNIEnv*, jobject, jboolean on) { engine.setMetronome (on); }
FN (void, setQuantise) (JNIEnv*, jobject, jdouble g) { engine.setQuantiseRecording (g); }
FN (void, setTrack) (JNIEnv*, jobject, jint i, jint type, jint sound, jfloat vol, jfloat pan, jboolean mute, jboolean solo)
{ engine.setTrack (i, (vega::TrackType) type, sound, vol, pan, mute, solo); }
FN (void, removeTrack) (JNIEnv*, jobject, jint i) { engine.removeTrack (i); }
FN (void, setClipNotes) (JNIEnv* env, jobject, jint t, jint s, jdouble len, jdoubleArray packed)
{
    std::vector<vega::NoteEv> notes;
    const jsize n = env->GetArrayLength (packed);
    std::vector<double> d ((size_t) n);
    env->GetDoubleArrayRegion (packed, 0, n, d.data());
    for (jsize i = 0; i + 3 < n; i += 4) notes.push_back ({ (int) d[(size_t) i], d[(size_t) i + 1], d[(size_t) i + 2], (int) d[(size_t) i + 3] });
    engine.setClipNotes (t, s, len, notes);
}
FN (jboolean, setClipAudioFile) (JNIEnv* env, jobject, jint t, jint s, jdouble len, jstring path)
{
    std::vector<float> l, r;
    if (! vega::readWav (str (env, path), host.sampleRate(), l, r)) return false;
    engine.setClipAudio (t, s, len, std::move (l), std::move (r));
    return true;
}
FN (void, clearClip) (JNIEnv*, jobject, jint t, jint s) { engine.clearClip (t, s); }
FN (void, play) (JNIEnv*, jobject) { engine.play(); }
FN (void, stop) (JNIEnv*, jobject) { engine.stop(); }
FN (void, launchClip) (JNIEnv*, jobject, jint t, jint s) { engine.launchClip (t, s); }
FN (void, stopTrack) (JNIEnv*, jobject, jint t) { engine.stopTrack (t); }
FN (void, launchScene) (JNIEnv*, jobject, jint s) { engine.launchScene (s); }
FN (void, noteOn) (JNIEnv*, jobject, jint t, jint n, jint v) { engine.noteOn (t, n, v); }
FN (void, noteOff) (JNIEnv*, jobject, jint t, jint n) { engine.noteOff (t, n); }
FN (void, startRecording) (JNIEnv*, jobject, jint t, jint s, jint bars, jboolean overdub) { engine.startRecording (t, s, bars, overdub); }
FN (void, stopRecording) (JNIEnv*, jobject) { engine.stopRecording(); }
FN (jdoubleArray, clipNotes) (JNIEnv* env, jobject, jint t, jint s)
{
    auto notes = engine.clipNotes (t, s);
    std::vector<double> d;
    for (auto& n : notes) { d.push_back (n.note); d.push_back (n.start); d.push_back (n.length); d.push_back (n.velocity); }
    auto arr = env->NewDoubleArray ((jsize) d.size());
    env->SetDoubleArrayRegion (arr, 0, (jsize) d.size(), d.data());
    return arr;
}
FN (jboolean, saveRecordedAudio) (JNIEnv* env, jobject, jint t, jint s, jstring path)
{
    std::vector<float> l, r;
    if (! engine.takeRecordedAudio (t, s, l, r)) return false;
    return vega::writeWav (str (env, path), l, r, host.sampleRate());
}
FN (jdoubleArray, state) (JNIEnv* env, jobject)
{
    auto s = engine.state();
    auto arr = env->NewDoubleArray ((jsize) s.size());
    env->SetDoubleArrayRegion (arr, 0, (jsize) s.size(), s.data());
    return arr;
}
FN (jboolean, renderClip) (JNIEnv* env, jobject, jint t, jint s, jint loops, jstring path)
{
    std::vector<float> l, r;
    engine.renderClipOffline (t, s, loops, l, r);
    if (l.empty()) return false;
    return vega::writeWav (str (env, path), l, r, host.sampleRate());
}

FN (jboolean, loadSampleKit) (JNIEnv* env, jobject, jint slot, jintArray notes, jobjectArray paths)
{
    const jsize n = env->GetArrayLength (notes);
    std::vector<int> ns ((size_t) n);
    env->GetIntArrayRegion (notes, 0, n, ns.data());
    std::vector<std::string> ps;
    for (jsize i = 0; i < env->GetArrayLength (paths); ++i)
    {
        auto js = (jstring) env->GetObjectArrayElement (paths, i);
        ps.push_back (str (env, js));
        env->DeleteLocalRef (js);
    }
    return engine.loadSampleKit (slot, ns, ps);
}
FN (void, setTrackSampleKit) (JNIEnv*, jobject, jint t, jint slot) { engine.setTrackSampleKit (t, slot); }
FN (void, setTrackWarmth) (JNIEnv*, jobject, jint t, jfloat a) { engine.setTrackWarmth (t, a); }
