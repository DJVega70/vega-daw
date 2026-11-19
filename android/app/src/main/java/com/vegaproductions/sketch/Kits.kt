package com.vegaproductions.sketch

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.runtime.mutableStateListOf
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.zip.ZipInputStream

/** A drum kit made of recorded samples: one WAV per pad (MIDI note). */
class Kit(val id: String, var name: String, val credit: String, val builtIn: Boolean, val dir: File,
          val pads: MutableMap<Int, Pair<String, String>>)   // note -> (pad name, file name in dir)

/**
 * The sample kits: the bundled vintage machines (copied out of the app on first run) plus kits you import
 * (a .zip sample pack or a handful of WAVs from Downloads / Drive). Each kit gets a slot in the engine.
 */
class KitLibrary(private val context: Context) {
    val kits = mutableStateListOf<Kit>()
    private val root: File get() = File(context.filesDir, "kits").apply { mkdirs() }
    private val slots = mutableMapOf<String, Int>()
    private var nextSlot = 0
    var lastImportReport: String = ""
        private set

    fun init() {
        installBundled()
        kits.clear()
        val dirs = (root.listFiles() ?: emptyArray()).filter { File(it, "kit.json").exists() }
        // bundled first in a fixed order, then yours, oldest first
        val order = listOf("vintage808", "linndrum", "cr78", "tr707")
        dirs.sortedWith(compareBy({ d -> order.indexOf(d.name).let { if (it < 0) 100 else it } }, { it.lastModified() }))
            .forEach { d -> read(d)?.let { kits += it } }
        reloadAll()
    }

    /** (Re)load every kit into the engine - after the audio device (sample rate) changes too */
    fun reloadAll() { kits.forEach { load(it) } }

    fun kit(id: String?): Kit? = kits.firstOrNull { it.id == id }
    fun slotOf(id: String?): Int = id?.let { slots[it] } ?: -1
    fun padName(id: String?, note: Int): String = kit(id)?.pads?.get(note)?.first ?: if (id == null) PAD_NAMES[note] ?: "" else "-"

    private fun installBundled() {
        val marker = File(root, ".bundled-v1")
        if (marker.exists()) return
        val am = context.assets
        for (kid in am.list("kits") ?: emptyArray()) {
            val files = am.list("kits/$kid") ?: continue
            if ("kit.json" !in files) continue   // e.g. CREDITS.txt
            val dst = File(root, kid).apply { mkdirs() }
            for (f in files) am.open("kits/$kid/$f").use { inp -> File(dst, f).outputStream().use { inp.copyTo(it) } }
        }
        marker.writeText("1")
    }

    private fun read(d: File): Kit? = runCatching {
        val o = JSONObject(File(d, "kit.json").readText())
        val pads = mutableMapOf<Int, Pair<String, String>>()
        val a = o.getJSONArray("pads")
        for (i in 0 until a.length()) { val p = a.getJSONObject(i); pads[p.getInt("note")] = p.getString("name") to p.getString("file") }
        Kit(o.optString("id", d.name), o.optString("name", d.name), o.optString("credit", ""), o.optBoolean("builtIn", !d.name.startsWith("user-")), d, pads)
    }.getOrNull()

    private fun write(k: Kit) {
        val o = JSONObject().put("id", k.id).put("name", k.name).put("credit", k.credit).put("builtIn", k.builtIn)
        val a = JSONArray()
        k.pads.toSortedMap().forEach { (note, p) -> a.put(JSONObject().put("note", note).put("name", p.first).put("file", p.second)) }
        o.put("pads", a)
        File(k.dir, "kit.json").writeText(o.toString(1))
    }

    private fun load(k: Kit) {
        val slot = slots.getOrPut(k.id) { nextSlot++ }
        val notes = k.pads.keys.toIntArray()
        val paths = notes.map { File(k.dir, k.pads[it]!!.second).absolutePath }.toTypedArray()
        NativeEngine.loadSampleKit(slot, notes, paths)
    }

    fun rename(k: Kit, name: String) { k.name = name; write(k); replace(k) }
    fun delete(k: Kit) { if (k.builtIn) return; k.dir.deleteRecursively(); kits.remove(k) }
    private fun replace(k: Kit) { val i = kits.indexOfFirst { it.id == k.id }; if (i >= 0) kits[i] = k else kits += k }

    /** Copy of a kit you can change (bundled kits stay as they are) */
    fun editableCopy(k: Kit): Kit {
        if (!k.builtIn) return k
        val id = "user-" + System.currentTimeMillis()
        val dir = File(root, id).apply { mkdirs() }
        k.pads.values.forEach { (_, f) -> File(k.dir, f).copyTo(File(dir, f), overwrite = true) }
        val copy = Kit(id, "My " + k.name, k.credit, false, dir, k.pads.toMutableMap())
        write(copy); kits += copy; load(copy)
        return copy
    }

    /** Put a WAV from the phone on one pad. Returns the kit that now has it (a copy, if the kit was a bundled one). */
    fun setPadSound(k: Kit, note: Int, uri: Uri): Kit? {
        val target = editableCopy(k)
        val name = displayName(uri)
        val file = "pad$note-${System.currentTimeMillis()}.wav"
        val ok = runCatching { context.contentResolver.openInputStream(uri)!!.use { inp -> File(target.dir, file).outputStream().use { inp.copyTo(it) } } }.isSuccess
        if (!ok || !isWav(File(target.dir, file))) { File(target.dir, file).delete(); lastImportReport = "That file isn't a WAV"; return null }
        target.pads[note]?.second?.let { old -> if (old != file && target.pads.values.count { it.second == old } == 1) File(target.dir, old).delete() }
        target.pads[note] = cleanName(name) to file
        write(target); replace(target); load(target)
        return target
    }

    /**
     * Import a kit: a .zip sample pack (any folder layout) or several WAVs. Sounds are put on pads from their
     * names ("kick", "snare", "clap", "hat"/"hh", "open", "rim", "tom", "crash", "ride", "cowbell", "shaker", "tamb", ...).
     */
    fun importKit(uris: List<Uri>): Kit? {
        val id = "user-" + System.currentTimeMillis()
        val dir = File(root, id).apply { mkdirs() }
        val found = mutableListOf<Pair<String, File>>()       // original name, saved file
        var kitName = ""
        var skipped = 0
        for (uri in uris) {
            val name = displayName(uri)
            if (name.endsWith(".zip", true)) {
                if (kitName.isEmpty()) kitName = name.substringBeforeLast('.')
                runCatching {
                    ZipInputStream(context.contentResolver.openInputStream(uri)!!.buffered()).use { z ->
                        var e = z.nextEntry
                        while (e != null) {
                            val n = e.name
                            if (!e.isDirectory && n.endsWith(".wav", true) && !n.contains("__MACOSX") && !n.substringAfterLast('/').startsWith("._")) {
                                val f = File(dir, "s${found.size}.wav")
                                f.outputStream().use { z.copyTo(it) }
                                if (isWav(f)) found += n to f else { f.delete(); skipped++ }
                            } else if (!e.isDirectory && !n.endsWith(".wav", true)) skipped++
                            e = z.nextEntry
                        }
                    }
                }
            } else {
                val f = File(dir, "s${found.size}.wav")
                runCatching { context.contentResolver.openInputStream(uri)!!.use { inp -> f.outputStream().use { inp.copyTo(it) } } }
                if (isWav(f)) found += name to f else { f.delete(); skipped++ }
            }
        }
        if (found.isEmpty()) { dir.deleteRecursively(); lastImportReport = "No WAV files found - the kit needs .wav samples"; return null }
        val map = autoMap(found.map { it.first })
        val pads = mutableMapOf<Int, Pair<String, String>>()
        map.forEach { (note, idx) -> val (orig, f) = found[idx]; pads[note] = cleanName(orig) to f.name }
        // keep only the files that made it onto a pad
        val used = pads.values.map { it.second }.toSet()
        found.forEach { (_, f) -> if (f.name !in used) f.delete() }
        val kit = Kit(id, kitName.ifEmpty { "My Kit ${kits.count { !it.builtIn } + 1}" }, "Imported", false, dir, pads)
        write(kit); kits += kit; load(kit)
        lastImportReport = "${pads.size} sounds on pads" + (if (found.size > pads.size) " (${found.size - pads.size} more in the pack weren't needed)" else "") +
            (if (skipped > 0) ", $skipped non-WAV files skipped" else "")
        return kit
    }

    private fun displayName(uri: Uri): String {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) return c.getString(0) ?: "sound.wav"
        }
        return uri.lastPathSegment?.substringAfterLast('/') ?: "sound.wav"
    }
    private fun isWav(f: File): Boolean = runCatching { f.inputStream().use { val b = ByteArray(12); it.read(b) == 12 && String(b, 0, 4) == "RIFF" && String(b, 8, 4) == "WAVE" } }.getOrDefault(false)
    private fun cleanName(n: String): String = n.substringAfterLast('/').substringBeforeLast('.').replace('_', ' ').replace('-', ' ').trim().take(14).ifEmpty { "Sound" }

    companion object {
        /** Which sample goes on which pad, from the file names (and folder names) */
        fun autoMap(names: List<String>): Map<Int, Int> {
            val lower = names.map { it.lowercase().replace('\\', '/') }
            val used = mutableSetOf<Int>()
            val result = linkedMapOf<Int, Int>()
            fun token(s: String, vararg keys: String) = keys.any { k ->
                val base = s.substringAfterLast('/')
                Regex("(^|[^a-z])${Regex.escape(k)}([^a-z]|$)").containsMatchIn(base) || Regex("(^|/)${Regex.escape(k)}[^/]*/").containsMatchIn(s)
                    || (k.length >= 4 && base.contains(k))
            }
            fun pick(note: Int, vararg keys: String, not: List<String> = emptyList()) {
                if (result.containsKey(note)) return
                // prefer shorter names (often the "main" sample) and the first in the pack's order
                val idx = lower.indices.filter { it !in used && token(lower[it], *keys) && not.none { n -> token(lower[it], n) } }
                    .minByOrNull { lower[it].substringAfterLast('/').length + it * 0.001 } ?: return
                result[note] = idx; used += idx
            }
            pick(36, "kick", "bd", "bassdrum", "bass drum", "kik")
            pick(38, "snare", "sd", "snr", not = listOf("rim"))
            pick(39, "clap", "cp", "handclap")
            pick(42, "closed", "chh", "ch", "hhc", "hh", "hat", "hihat", "hi-hat", not = listOf("open", "oh", "ohh"))
            pick(46, "open", "ohh", "oh", "hho")
            pick(37, "rim", "rimshot", "rs", "stick", "sidestick")
            pick(40, "snare", "sd", "snr", not = listOf("rim"))
            pick(44, "pedal", "ph", "hat", "hh", "hihat", not = listOf("open", "oh"))
            pick(41, "low tom", "lt", "tom low", "tom1", "floor", "tom")
            pick(45, "mid tom", "mt", "tom mid", "tom2", "tom")
            pick(48, "hi tom", "ht", "tom hi", "high tom", "tom3", "tom")
            pick(49, "crash", "cy", "cymbal", "cym")
            pick(51, "ride", "rd")
            pick(56, "cowbell", "cb", "cow", "bell")
            pick(70, "shaker", "shake", "maracas", "ma", "cabasa")
            pick(54, "tamb", "tambourine", "tamborine")
            // anything left fills the empty pads (percussion, fx, extra kicks...)
            val freePads = PAD_NOTES.filter { !result.containsKey(it) }
            val rest = lower.indices.filter { it !in used }.sortedBy { it }
            freePads.zip(rest).forEach { (note, idx) -> result[note] = idx }
            return result
        }
    }
}
