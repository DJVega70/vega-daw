package com.vegaproductions.sketch

/** The C++ music engine (drums, synth, clip launcher, recording). See app/src/main/cpp. */
object NativeEngine {
    init { System.loadLibrary("vegasketch") }

    external fun start(): Boolean
    external fun shutdown()
    external fun sampleRate(): Int
    external fun startInput(): Boolean
    external fun stopInput()

    external fun setTempo(bpm: Double)
    external fun setBeatsPerBar(n: Int)
    external fun setMetronome(on: Boolean)
    external fun setQuantise(grid: Double)
    external fun setTrack(index: Int, type: Int, sound: Int, volume: Float, pan: Float, mute: Boolean, solo: Boolean)
    external fun removeTrack(index: Int)
    /** notes packed as [note, start, length, velocity] * n, in beats */
    external fun setClipNotes(track: Int, scene: Int, lengthBeats: Double, packed: DoubleArray)
    external fun setClipAudioFile(track: Int, scene: Int, lengthBeats: Double, path: String): Boolean
    external fun clearClip(track: Int, scene: Int)
    /** a sample kit in engine slot 'slot': notes[i] plays paths[i] (WAV) */
    external fun loadSampleKit(slot: Int, notes: IntArray, paths: Array<String>): Boolean
    external fun setTrackSampleKit(track: Int, slot: Int)
    external fun setTrackWarmth(track: Int, amount: Float)

    external fun play()
    external fun stop()
    external fun launchClip(track: Int, scene: Int)
    external fun stopTrack(track: Int)
    external fun launchScene(scene: Int)
    external fun noteOn(track: Int, note: Int, velocity: Int)
    external fun noteOff(track: Int, note: Int)

    external fun startRecording(track: Int, scene: Int, bars: Int, overdub: Boolean)
    external fun stopRecording()
    external fun clipNotes(track: Int, scene: Int): DoubleArray
    external fun saveRecordedAudio(track: Int, scene: Int, path: String): Boolean

    /** [beat, playing, recording, countingIn, inputLevel, audioTakeReady, then per track (8): playingScene, queuedScene, progress, level] */
    external fun state(): DoubleArray
    external fun renderClip(track: Int, scene: Int, loops: Int, path: String): Boolean
}
