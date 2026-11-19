package com.vegaproductions.sketch

import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.provider.MediaStore
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

const val MAX_TRACKS = 8
const val SCENES = 8

enum class TrackType(val code: Int, val label: String) { DRUMS(0, "drums"), KEYS(1, "keys"), AUDIO(2, "audio") }

// The same order as the C++ engine (Dsp.h)
val KIT_NAMES = listOf("House Classic", "Vega 808", "Vega 909", "Deep Soul", "Lo-Fi Dusty")
val PRESET_NAMES = listOf("Soul Keys", "Warm Pad", "Deep Bass", "House Organ", "Pluck", "Strings", "Bright Lead", "Garage Bass")
val PAD_NOTES = listOf(36, 38, 39, 42, 37, 40, 44, 46, 41, 45, 48, 49, 56, 70, 54, 51)
val PAD_NAMES = mapOf(36 to "Kick", 37 to "Rim", 38 to "Snare", 39 to "Clap", 40 to "Snare 2", 41 to "Low Tom", 42 to "Closed Hat", 44 to "Pedal Hat",
    45 to "Mid Tom", 46 to "Open Hat", 48 to "High Tom", 49 to "Crash", 51 to "Ride", 54 to "Tamb", 56 to "Cowbell", 70 to "Shaker")
val TRACK_COLOURS = listOf(0xFFFFA25EL, 0xFF6FB7FFL, 0xFF7EE87AL, 0xFFE8708AL, 0xFFA58BFFL, 0xFFE8B448L, 0xFF4CD6C8L, 0xFFF286D0L)

data class Note(val note: Int, val start: Double, val length: Double, val velocity: Int)

class Clip(var name: String, var bars: Int, var notes: MutableList<Note> = mutableListOf(), var audioFile: String? = null) {
    fun copy() = Clip(name, bars, notes.toMutableList(), audioFile)
}

class Track(var name: String, var type: TrackType, var sound: Int = 0, var volume: Float = 0.8f, var pan: Float = 0f,
            var mute: Boolean = false, var solo: Boolean = false, var colour: Long = TRACK_COLOURS[0],
            var kitId: String? = null,          // drums: a sample kit (null = the synthesized kit 'sound')
            var warmth: Float = 0.3f) {         // tape-style colour, 0..1
    val clips: Array<Clip?> = arrayOfNulls(SCENES)
}

fun soundName(t: Track, kits: KitLibrary?): String = when (t.type) {
    TrackType.DRUMS -> kits?.kit(t.kitId)?.name ?: (KIT_NAMES.getOrElse(t.sound) { "" } + if (t.kitId == null) " (synth)" else "")
    TrackType.KEYS -> PRESET_NAMES.getOrElse(t.sound) { "" }
    TrackType.AUDIO -> "Mic"
}

class Sketch(var id: String = UUID.randomUUID().toString(), var name: String = "Sketch", var bpm: Double = 122.0, var beatsPerBar: Int = 4) {
    val tracks = mutableListOf<Track>()
    val scenes = MutableList(SCENES) { "Scene ${it + 1}" }
    var modified: Long = System.currentTimeMillis()
}

/** Saves sketches in the app's private storage, one folder each (sketch.json + audio/). */
class SketchStore(private val context: Context, val kits: KitLibrary) {
    val root: File get() = File(context.filesDir, "sketches").apply { mkdirs() }
    fun folder(s: Sketch) = File(root, s.id).apply { mkdirs() }
    fun audioFolder(s: Sketch) = File(folder(s), "audio").apply { mkdirs() }

    fun save(s: Sketch) {
        s.modified = System.currentTimeMillis()
        File(folder(s), "sketch.json").writeText(toJson(s, withRenders = false).toString(2))
    }

    fun list(): List<Pair<String, String>> =
        (root.listFiles() ?: emptyArray()).filter { File(it, "sketch.json").exists() }
            .sortedByDescending { File(it, "sketch.json").lastModified() }
            .mapNotNull { dir -> runCatching { dir.name to JSONObject(File(dir, "sketch.json").readText()).getString("name") }.getOrNull() }

    fun load(id: String): Sketch? = runCatching { fromJson(JSONObject(File(File(root, id), "sketch.json").readText()), id) }.getOrNull()
    fun delete(id: String) { File(root, id).deleteRecursively() }

    // ---------------------------------------------------------------- JSON (also read by the desktop DAW's File > Import phone sketch)
    fun toJson(s: Sketch, withRenders: Boolean): JSONObject {
        val o = JSONObject()
        o.put("format", "vega-sketch"); o.put("version", 1); o.put("app", "Vega Sketch")
        o.put("name", s.name); o.put("bpm", s.bpm); o.put("beatsPerBar", s.beatsPerBar)
        o.put("scenes", JSONArray(s.scenes))
        val tracks = JSONArray()
        s.tracks.forEachIndexed { ti, t ->
            val to = JSONObject()
            to.put("name", t.name); to.put("type", t.type.label); to.put("sound", t.sound); to.put("soundName", soundName(t, kits))
            to.put("warmth", t.warmth.toDouble())
            t.kitId?.let { id ->
                to.put("kitId", id)
                kits.kit(id)?.let { k ->
                    // the kit itself travels with an export, so the desktop can load the same sounds into its sampler
                    if (withRenders) {
                        val pads = JSONArray()
                        k.pads.toSortedMap().forEach { (note, p) -> pads.put(JSONObject().put("note", note).put("name", p.first).put("file", "kits/${k.id}/${p.second}")) }
                        to.put("kit", JSONObject().put("id", k.id).put("name", k.name).put("credit", k.credit).put("pads", pads))
                    }
                }
            }
            to.put("volume", t.volume.toDouble()); to.put("pan", t.pan.toDouble()); to.put("mute", t.mute); to.put("solo", t.solo)
            to.put("colour", String.format("#%06X", t.colour and 0xFFFFFF))
            val clips = JSONArray()
            t.clips.forEachIndexed { si, c ->
                if (c == null) return@forEachIndexed
                val co = JSONObject()
                co.put("scene", si); co.put("name", c.name); co.put("bars", c.bars); co.put("lengthBeats", (c.bars * s.beatsPerBar).toDouble())
                val notes = JSONArray()
                c.notes.forEach { n -> notes.put(JSONArray().put(n.note).put(n.start).put(n.length).put(n.velocity)) }
                co.put("notes", notes)
                c.audioFile?.let { co.put("audio", "audio/$it") }
                if (withRenders) co.put("render", "renders/t${ti}s$si.wav")
                clips.put(co)
            }
            to.put("clips", clips)
            tracks.put(to)
        }
        o.put("tracks", tracks)
        return o
    }

    private fun fromJson(o: JSONObject, id: String): Sketch {
        val s = Sketch(id, o.optString("name", "Sketch"), o.optDouble("bpm", 122.0), o.optInt("beatsPerBar", 4))
        o.optJSONArray("scenes")?.let { a -> for (i in 0 until minOf(SCENES, a.length())) s.scenes[i] = a.getString(i) }
        val ta = o.optJSONArray("tracks") ?: JSONArray()
        for (i in 0 until minOf(MAX_TRACKS, ta.length())) {
            val to = ta.getJSONObject(i)
            val type = TrackType.values().firstOrNull { it.label == to.optString("type") } ?: TrackType.DRUMS
            val t = Track(to.optString("name", "Track"), type, to.optInt("sound"), to.optDouble("volume", 0.8).toFloat(), to.optDouble("pan", 0.0).toFloat(),
                to.optBoolean("mute"), to.optBoolean("solo"), runCatching { 0xFF000000L or to.optString("colour").removePrefix("#").toLong(16) }.getOrDefault(TRACK_COLOURS[i % 8]),
                to.optString("kitId", "").ifEmpty { null }, to.optDouble("warmth", 0.3).toFloat())
            val ca = to.optJSONArray("clips") ?: JSONArray()
            for (k in 0 until ca.length()) {
                val co = ca.getJSONObject(k)
                val sc = co.optInt("scene"); if (sc !in 0 until SCENES) continue
                val notes = mutableListOf<Note>()
                val na = co.optJSONArray("notes") ?: JSONArray()
                for (j in 0 until na.length()) { val n = na.getJSONArray(j); notes += Note(n.getInt(0), n.getDouble(1), n.getDouble(2), n.getInt(3)) }
                t.clips[sc] = Clip(co.optString("name", "Clip"), co.optInt("bars", 1), notes, co.optString("audio", "").removePrefix("audio/").ifEmpty { null })
            }
            s.tracks += t
        }
        return s
    }

    // ---------------------------------------------------------------- export for the desktop: a .vegasketch (zip) with the audio and a print of every clip
    fun export(s: Sketch): File {
        val out = File(context.cacheDir, "exports").apply { mkdirs() }
        out.listFiles()?.forEach { it.delete() }
        val renders = File(context.cacheDir, "renders").apply { deleteRecursively(); mkdirs() }
        s.tracks.forEachIndexed { ti, t -> t.clips.forEachIndexed { si, c -> if (c != null) NativeEngine.renderClip(ti, si, 1, File(renders, "t${ti}s$si.wav").absolutePath) } }
        val safe = s.name.replace(Regex("[^A-Za-z0-9 _-]"), "").trim().ifEmpty { "Sketch" }
        val zip = File(out, "$safe.vegasketch")
        ZipOutputStream(zip.outputStream().buffered()).use { z ->
            z.putNextEntry(ZipEntry("sketch.json")); z.write(toJson(s, withRenders = true).toString(2).toByteArray()); z.closeEntry()
            audioFolder(s).listFiles()?.forEach { f -> z.putNextEntry(ZipEntry("audio/${f.name}")); f.inputStream().use { it.copyTo(z) }; z.closeEntry() }
            renders.listFiles()?.forEach { f -> z.putNextEntry(ZipEntry("renders/${f.name}")); f.inputStream().use { it.copyTo(z) }; z.closeEntry() }
            s.tracks.mapNotNull { kits.kit(it.kitId) }.distinctBy { it.id }.forEach { k ->
                k.pads.values.map { it.second }.distinct().forEach { fn ->
                    val f = File(k.dir, fn)
                    if (f.exists()) { z.putNextEntry(ZipEntry("kits/${k.id}/$fn")); f.inputStream().use { it.copyTo(z) }; z.closeEntry() }
                }
            }
        }
        return zip
    }

    /** Copies an export into Downloads/Vega Sketch (for USB transfer / Google Drive upload). Returns where. */
    fun saveToDownloads(zip: File): String {
        if (Build.VERSION.SDK_INT >= 29) {
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, zip.name)
                put(MediaStore.Downloads.MIME_TYPE, "application/zip")
                put(MediaStore.Downloads.RELATIVE_PATH, "Download/Vega Sketch")
            }
            val uri = context.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: return ""
            context.contentResolver.openOutputStream(uri)?.use { o -> zip.inputStream().use { it.copyTo(o) } }
            return "Download/Vega Sketch/${zip.name}"
        }
        @Suppress("DEPRECATION")
        val dir = File(android.os.Environment.getExternalStoragePublicDirectory(android.os.Environment.DIRECTORY_DOWNLOADS), "Vega Sketch").apply { mkdirs() }
        zip.copyTo(File(dir, zip.name), overwrite = true)
        return "Download/Vega Sketch/${zip.name}"
    }
}
