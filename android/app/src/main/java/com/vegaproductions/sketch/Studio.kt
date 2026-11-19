package com.vegaproductions.sketch

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File
import kotlin.math.roundToInt

/** The app's state: the open sketch, what's selected, and the engine kept in step with it. */
class Studio(val store: SketchStore) {
    var sketch by mutableStateOf(Sketch())
        private set
    /** bumped after every change to the sketch, so the screens redraw */
    var version by mutableIntStateOf(0)
        private set
    var selectedTrack by mutableIntStateOf(0)
    var selectedScene by mutableIntStateOf(0)
    var recordBars by mutableIntStateOf(2)
    var quantise by mutableStateOf(true)
    var metronome by mutableStateOf(false)
    var overdub by mutableStateOf(true)
    var engineState by mutableStateOf(DoubleArray(6 + 4 * MAX_TRACKS))
    var message by mutableStateOf<String?>(null)
    private var undoClip: Pair<Pair<Int, Int>, Clip?>? = null
    private var recordingTarget: Pair<Int, Int>? = null

    fun changed() { version++ }

    // ---------------------------------------------------------------- sketches
    fun newSketch() {
        save()
        val s = Sketch(name = "Sketch " + (store.list().size + 1))
        s.tracks += Track("Drums", TrackType.DRUMS, 0, 0.85f, colour = TRACK_COLOURS[0])
        s.tracks += Track("Keys", TrackType.KEYS, 0, 0.7f, colour = TRACK_COLOURS[1])
        s.tracks += Track("Bass", TrackType.KEYS, 2, 0.75f, colour = TRACK_COLOURS[2])
        s.tracks += Track("Vocals", TrackType.AUDIO, 0, 0.9f, colour = TRACK_COLOURS[3])
        open(s)
    }
    fun open(s: Sketch) {
        NativeEngine.stop()
        for (i in 0 until MAX_TRACKS) NativeEngine.removeTrack(i)
        sketch = s
        selectedTrack = 0; selectedScene = 0
        NativeEngine.setTempo(s.bpm); NativeEngine.setBeatsPerBar(s.beatsPerBar)
        syncAll()
        store.save(s)
        changed()
    }
    fun openId(id: String) { save(); store.load(id)?.let { open(it) } }
    fun save() { if (sketch.tracks.isNotEmpty()) store.save(sketch) }

    private fun syncAll() {
        sketch.tracks.forEachIndexed { i, _ -> syncTrack(i); for (s in 0 until SCENES) syncClip(i, s) }
    }
    fun syncTrack(i: Int) {
        val t = sketch.tracks.getOrNull(i) ?: return
        NativeEngine.setTrack(i, t.type.code, t.sound, t.volume, t.pan, t.mute, t.solo)
    }
    fun syncClip(t: Int, s: Int) {
        val track = sketch.tracks.getOrNull(t) ?: return
        val c = track.clips[s]
        if (c == null) { NativeEngine.clearClip(t, s); return }
        val len = (c.bars * sketch.beatsPerBar).toDouble()
        if (track.type == TrackType.AUDIO) {
            val f = c.audioFile?.let { File(store.audioFolder(sketch), it) }
            if (f == null || !f.exists() || !NativeEngine.setClipAudioFile(t, s, len, f.absolutePath)) NativeEngine.clearClip(t, s)
        } else {
            val packed = DoubleArray(c.notes.size * 4)
            c.notes.forEachIndexed { k, n -> packed[k * 4] = n.note.toDouble(); packed[k * 4 + 1] = n.start; packed[k * 4 + 2] = n.length; packed[k * 4 + 3] = n.velocity.toDouble() }
            NativeEngine.setClipNotes(t, s, len, packed)
        }
    }

    // ---------------------------------------------------------------- song settings
    fun setTempo(bpm: Double) { sketch.bpm = bpm.coerceIn(60.0, 200.0); NativeEngine.setTempo(sketch.bpm); changed() }
    fun toggleMetronome() { metronome = !metronome; NativeEngine.setMetronome(metronome) }
    fun rename(name: String) { sketch.name = name.ifBlank { sketch.name }; save(); changed() }

    // ---------------------------------------------------------------- tracks
    fun addTrack(type: TrackType) {
        if (sketch.tracks.size >= MAX_TRACKS) { message = "8 tracks is the most a sketch can have"; return }
        val n = sketch.tracks.count { it.type == type } + 1
        val name = when (type) { TrackType.DRUMS -> "Drums $n"; TrackType.KEYS -> "Keys $n"; TrackType.AUDIO -> "Audio $n" }
        sketch.tracks += Track(name, type, 0, 0.8f, colour = TRACK_COLOURS[sketch.tracks.size % TRACK_COLOURS.size])
        selectedTrack = sketch.tracks.size - 1
        syncTrack(selectedTrack)
        changed()
    }
    fun deleteTrack(i: Int) {
        if (i !in sketch.tracks.indices) return
        sketch.tracks.removeAt(i)
        for (k in 0 until MAX_TRACKS) NativeEngine.removeTrack(k)
        syncAll()
        selectedTrack = selectedTrack.coerceIn(0, maxOf(0, sketch.tracks.size - 1))
        changed()
    }
    fun updateTrack(i: Int, block: Track.() -> Unit) { sketch.tracks.getOrNull(i)?.let { it.block(); syncTrack(i); changed() } }

    // ---------------------------------------------------------------- clips
    fun clip(t: Int, s: Int): Clip? = sketch.tracks.getOrNull(t)?.clips?.getOrNull(s)
    fun ensureClip(t: Int, s: Int, bars: Int = recordBars): Clip? {
        val track = sketch.tracks.getOrNull(t) ?: return null
        return track.clips[s] ?: Clip(defaultClipName(track, s), bars).also { track.clips[s] = it; syncClip(t, s); changed() }
    }
    private fun defaultClipName(t: Track, s: Int) = when (t.type) { TrackType.DRUMS -> "Beat ${s + 1}"; TrackType.KEYS -> "${t.name} ${s + 1}"; TrackType.AUDIO -> "Take ${s + 1}" }
    fun setClipBars(t: Int, s: Int, bars: Int) {
        val c = clip(t, s) ?: return
        val newLen = (bars * sketch.beatsPerBar).toDouble()
        if (bars > c.bars && c.notes.isNotEmpty()) {
            // longer: repeat what's there to fill the new length
            val oldLen = (c.bars * sketch.beatsPerBar).toDouble()
            val reps = (bars / c.bars) - 1
            val base = c.notes.toList()
            for (r in 1..reps) base.forEach { n -> c.notes += n.copy(start = n.start + r * oldLen) }
        } else c.notes.removeAll { it.start >= newLen }
        c.bars = bars
        syncClip(t, s); changed()
    }
    fun deleteClip(t: Int, s: Int) {
        val track = sketch.tracks.getOrNull(t) ?: return
        undoClip = (t to s) to track.clips[s]?.copy()
        track.clips[s] = null
        syncClip(t, s); changed()
    }
    fun duplicateClip(t: Int, s: Int) {
        val track = sketch.tracks.getOrNull(t) ?: return
        val c = track.clips[s] ?: return
        val target = (s + 1 until SCENES).firstOrNull { track.clips[it] == null } ?: run { message = "No empty slot below"; return }
        var copy = c.copy()
        if (track.type == TrackType.AUDIO && c.audioFile != null) {
            val src = File(store.audioFolder(sketch), c.audioFile!!)
            val name = "${System.currentTimeMillis()}.wav"
            runCatching { src.copyTo(File(store.audioFolder(sketch), name)) }
            copy = Clip(c.name, c.bars, mutableListOf(), name)
        }
        copy.name = c.name + " copy"
        track.clips[target] = copy
        syncClip(t, target); selectedScene = target; changed()
    }
    fun clearNotes(t: Int, s: Int) { val c = clip(t, s) ?: return; undoClip = (t to s) to c.copy(); c.notes.clear(); syncClip(t, s); changed() }
    fun quantiseClip(t: Int, s: Int) {
        val c = clip(t, s) ?: return
        undoClip = (t to s) to c.copy()
        val len = (c.bars * sketch.beatsPerBar).toDouble()
        c.notes.replaceAll { n -> n.copy(start = ((n.start * 4).roundToInt() / 4.0).let { if (it >= len) 0.0 else it }) }
        syncClip(t, s); changed()
    }
    fun undo() {
        val u = undoClip ?: run { message = "Nothing to undo"; return }
        val (pos, c) = u
        sketch.tracks.getOrNull(pos.first)?.clips?.set(pos.second, c)
        syncClip(pos.first, pos.second); undoClip = null; changed()
    }

    /** Step sequencer: switch a drum hit on / off at a 1/16 step */
    fun toggleStep(t: Int, s: Int, note: Int, step: Int) {
        val c = ensureClip(t, s, 1) ?: return
        val start = step * 0.25
        val existing = c.notes.firstOrNull { it.note == note && kotlin.math.abs(it.start - start) < 0.01 }
        if (existing != null) c.notes.remove(existing) else c.notes += Note(note, start, 0.25, if (step % 4 == 0) 112 else 96)
        syncClip(t, s); changed()
    }
    fun hasStep(c: Clip?, note: Int, step: Int) = c?.notes?.any { it.note == note && kotlin.math.abs(it.start - step * 0.25) < 0.01 } == true

    // ---------------------------------------------------------------- playing
    fun launch(t: Int, s: Int) {
        val st = playingScene(t)
        if (st == s && isPlaying()) NativeEngine.stopTrack(t) else NativeEngine.launchClip(t, s)
    }
    fun launchScene(s: Int) = NativeEngine.launchScene(s)
    fun togglePlay() { if (isPlaying()) stopAll() else NativeEngine.play() }
    fun stopAll() { if (isRecording()) stopRecording(); NativeEngine.stop() }
    fun noteOn(t: Int, note: Int, vel: Int) = NativeEngine.noteOn(t, note, vel)
    fun noteOff(t: Int, note: Int) = NativeEngine.noteOff(t, note)

    // ---------------------------------------------------------------- recording into the selected slot
    fun toggleRecord() { if (isRecording()) stopRecording() else startRecording() }
    fun startRecording() {
        val t = selectedTrack; val s = selectedScene
        val track = sketch.tracks.getOrNull(t) ?: run { message = "Add a track first"; return }
        undoClip = (t to s) to track.clips[s]?.copy()
        if (track.type == TrackType.AUDIO) {
            if (!NativeEngine.startInput()) { message = "Microphone unavailable - allow microphone access for Vega Sketch"; return }
            NativeEngine.startRecording(t, s, recordBars, false)
        } else {
            NativeEngine.setQuantise(if (quantise) 0.25 else 0.0)
            val keep = overdub && track.clips[s] != null
            if (!keep) { track.clips[s] = Clip(defaultClipName(track, s), recordBars); syncClip(t, s) }
            NativeEngine.startRecording(t, s, track.clips[s]!!.bars, keep)
        }
        recordingTarget = t to s
        changed()
    }
    fun stopRecording() {
        val (t, s) = recordingTarget ?: return
        val track = sketch.tracks.getOrNull(t)
        NativeEngine.stopRecording()
        if (track != null && track.type != TrackType.AUDIO) collectNotes(t, s)
        // audio: collected by poll() once the engine has the take
        if (track?.type != TrackType.AUDIO) recordingTarget = null
        changed()
    }
    private fun collectNotes(t: Int, s: Int) {
        val c = clip(t, s) ?: return
        val d = NativeEngine.clipNotes(t, s)
        c.notes = MutableList(d.size / 4) { k -> Note(d[k * 4].toInt(), d[k * 4 + 1], d[k * 4 + 2], d[k * 4 + 3].toInt()) }
        save()
    }

    /** called ~30x a second by the UI */
    fun poll() {
        engineState = NativeEngine.state()
        val target = recordingTarget ?: return
        val track = sketch.tracks.getOrNull(target.first) ?: return
        if (track.type == TrackType.AUDIO && engineState[5] > 0.5) {
            val name = "${System.currentTimeMillis()}.wav"
            val f = File(store.audioFolder(sketch), name)
            if (NativeEngine.saveRecordedAudio(target.first, target.second, f.absolutePath)) {
                track.clips[target.second] = Clip(defaultClipName(track, target.second), recordBars, mutableListOf(), name)
                syncClip(target.first, target.second)
                NativeEngine.launchClip(target.first, target.second)
                save()
            }
            NativeEngine.stopInput()
            recordingTarget = null
            changed()
        } else if (track.type != TrackType.AUDIO && engineState[2] > 0.5) {
            // live view of what's being recorded
            val d = NativeEngine.clipNotes(target.first, target.second)
            val c = clip(target.first, target.second)
            if (c != null && d.size / 4 != c.notes.size) { c.notes = MutableList(d.size / 4) { k -> Note(d[k * 4].toInt(), d[k * 4 + 1], d[k * 4 + 2], d[k * 4 + 3].toInt()) }; changed() }
        }
    }

    fun isPlaying() = engineState[1] > 0.5
    fun isRecording() = recordingTarget != null
    fun isCountingIn() = engineState[3] > 0.5
    fun beat() = engineState[0]
    fun playingScene(t: Int) = engineState.getOrElse(6 + t * 4) { -1.0 }.toInt()
    fun queuedScene(t: Int) = engineState.getOrElse(7 + t * 4) { -1.0 }.toInt()
    fun progress(t: Int) = engineState.getOrElse(8 + t * 4) { 0.0 }
    fun level(t: Int) = engineState.getOrElse(9 + t * 4) { 0.0 }
    fun inputLevel() = engineState[4]
}
