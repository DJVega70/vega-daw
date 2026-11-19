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
    private var engineRunning = false
    private var afterMic: (() -> Unit)? = null
    private val micPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) afterMic?.invoke() else toast("Vega Sketch needs the microphone to record audio")
        afterMic = null
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        studio = Studio(SketchStore(this))
        startEngine()
        val last = studio.store.list().firstOrNull()?.first
        if (last != null) studio.openId(last) else studio.newSketch()
        setContent {
            VegaTheme {
                VegaSketchApp(
                    studio,
                    withMic = { action -> withMic(action) },
                    onExport = { export(share = true) },
                    onSaveToDownloads = { export(share = false) })
            }
        }
    }

    private fun startEngine() { if (!engineRunning) engineRunning = NativeEngine.start() }

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
