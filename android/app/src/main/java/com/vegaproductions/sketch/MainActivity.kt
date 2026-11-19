package com.vegaproductions.sketch

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    private lateinit var studio: Studio
    private lateinit var kits: KitLibrary
    private var padTarget: Pair<Int, Int>? = null
    private val pickKit = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@registerForActivityResult
        toast("Importing the kit...")
        lifecycleScope.launch {
            val kit = withContext(Dispatchers.IO) { kits.importKit(uris) }
            if (kit != null) studio.updateTrack(studio.selectedTrack) { if (type == TrackType.DRUMS) kitId = kit.id }
            toast(if (kit != null) "\"${kit.name}\": ${kits.lastImportReport}" else kits.lastImportReport)
        }
    }
    private val pickPad = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val (ti, note) = padTarget ?: return@registerForActivityResult
        padTarget = null
        if (uri == null) return@registerForActivityResult
        val track = studio.sketch.tracks.getOrNull(ti) ?: return@registerForActivityResult
        val kit = kits.kit(track.kitId) ?: kits.kit(DEFAULT_KIT) ?: return@registerForActivityResult
        lifecycleScope.launch {
            val k = withContext(Dispatchers.IO) { kits.setPadSound(kit, note, uri) }
            if (k != null) { studio.updateTrack(ti) { kitId = k.id }; toast("Pad loaded" + if (k.id != kit.id) " (into your copy, \"${k.name}\")" else "") }
            else toast(kits.lastImportReport)
        }
    }
    private var engineRunning = false
    private var afterMic: (() -> Unit)? = null
    private val micPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) afterMic?.invoke() else toast("Vega Sketch needs the microphone to record audio")
        afterMic = null
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        kits = KitLibrary(this)
        studio = Studio(SketchStore(this, kits))
        startEngine()
        val last = studio.store.list().firstOrNull()?.first
        if (last != null) studio.openId(last) else studio.newSketch()
        setContent {
            VegaTheme {
                VegaSketchApp(
                    studio,
                    withMic = { action -> withMic(action) },
                    onExport = { export(share = true) },
                    onSaveToDownloads = { export(share = false) },
                    onImportKit = { pickKit.launch(arrayOf("application/zip", "application/x-zip-compressed", "audio/wav", "audio/x-wav", "audio/*", "application/octet-stream")) },
                    onPickPadSound = { ti, note -> padTarget = ti to note; pickPad.launch(arrayOf("audio/wav", "audio/x-wav", "audio/*")) })
            }
        }
    }

    private fun startEngine() {
        if (engineRunning) return
        engineRunning = NativeEngine.start()
        // samples are loaded at the device's sample rate, so (re)load them whenever the audio starts
        if (::kits.isInitialized) { if (kits.kits.isEmpty()) kits.init() else kits.reloadAll() }
        if (::studio.isInitialized) studio.resyncTracks()
    }

    override fun onStart() { super.onStart(); startEngine(); if (::studio.isInitialized) studio.open(studio.sketch) }
    override fun onStop() {
        super.onStop()
        if (::studio.isInitialized) { if (studio.isRecording()) studio.stopRecording(); studio.save() }
        NativeEngine.stop(); NativeEngine.shutdown(); engineRunning = false
    }

    private fun withMic(action: () -> Unit) {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) action()
        else { afterMic = action; micPermission.launch(Manifest.permission.RECORD_AUDIO) }
    }

    private fun export(share: Boolean) {
        studio.save()
        toast("Getting the sketch ready...")
        lifecycleScope.launch {
            val zip = withContext(Dispatchers.Default) { studio.store.export(studio.sketch) }
            if (share) {
                val uri = FileProvider.getUriForFile(this@MainActivity, "com.vegaproductions.sketch.files", zip)
                val send = Intent(Intent.ACTION_SEND).apply {
                    type = "application/zip"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    putExtra(Intent.EXTRA_SUBJECT, zip.name)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                startActivity(Intent.createChooser(send, "Send ${zip.name} to your PC"))
            } else {
                val where = withContext(Dispatchers.IO) { studio.store.saveToDownloads(zip) }
                toast(if (where.isEmpty()) "Couldn't save to Downloads" else "Saved to $where")
            }
        }
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()
}
