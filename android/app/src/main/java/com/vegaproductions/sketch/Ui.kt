package com.vegaproductions.sketch

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlin.math.floor

// ---------------------------------------------------------------- Vega colours
val Navy = Color(0xFF0B1426)
val Panel = Color(0xFF14203A)
val PanelHi = Color(0xFF1C2B4A)
val Gold = Color(0xFFD4AF37)
val GoldDim = Color(0xFF8C7330)
val TextHi = Color(0xFFECEDF2)
val TextDim = Color(0xFF8A93A8)
val RecRed = Color(0xFFE04848)
val PlayGreen = Color(0xFF7EE87A)

@Composable
fun VegaTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = darkColorScheme(primary = Gold, onPrimary = Navy, background = Navy, surface = Panel, onSurface = TextHi,
        surfaceVariant = PanelHi, onSurfaceVariant = TextDim, secondary = Gold, error = RecRed), content = content)
}

enum class Tab(val label: String) { SONG("Song"), BEATS("Beats"), KEYS("Keys"), RECORD("Record") }

@Composable
fun VegaSketchApp(studio: Studio, withMic: (() -> Unit) -> Unit, onExport: () -> Unit, onSaveToDownloads: () -> Unit) {
    var tab by remember { mutableStateOf(Tab.SONG) }
    LaunchedEffect(Unit) { while (true) { studio.poll(); delay(33) } }
    val snack = remember { SnackbarHostState() }
    LaunchedEffect(studio.message) { studio.message?.let { snack.showSnackbar(it); studio.message = null } }
    Scaffold(
        containerColor = Navy,
        snackbarHost = { SnackbarHost(snack) },
        topBar = { TopBar(studio, withMic, onExport, onSaveToDownloads, onEdit = { tab = it }) },
        bottomBar = {
            NavigationBar(containerColor = Panel) {
                Tab.values().forEach { t ->
                    NavigationBarItem(selected = tab == t, onClick = { tab = t },
                        icon = { Icon(when (t) { Tab.SONG -> Icons.Filled.GridView; Tab.BEATS -> Icons.Filled.Apps; Tab.KEYS -> Icons.Filled.Piano; Tab.RECORD -> Icons.Filled.Mic }, t.label) },
                        label = { Text(t.label) },
                        colors = NavigationBarItemDefaults.colors(selectedIconColor = Navy, indicatorColor = Gold, selectedTextColor = Gold, unselectedIconColor = TextDim, unselectedTextColor = TextDim))
                }
            }
        }
    ) { pad ->
        Box(Modifier.padding(pad).fillMaxSize()) {
            key(studio.version) { }   // read so every change redraws
            when (tab) {
                Tab.SONG -> SongScreen(studio, onEdit = { tab = it })
                Tab.BEATS -> BeatsScreen(studio)
                Tab.KEYS -> KeysScreen(studio)
                Tab.RECORD -> RecordScreen(studio, withMic)
            }
        }
    }
}

// ---------------------------------------------------------------- top bar: song, tempo, transport
@Composable
fun TopBar(studio: Studio, withMic: (() -> Unit) -> Unit, onExport: () -> Unit, onSaveToDownloads: () -> Unit, onEdit: (Tab) -> Unit) {
    var menu by remember { mutableStateOf(false) }
    var tempoDialog by remember { mutableStateOf(false) }
    var renameDialog by remember { mutableStateOf(false) }
    var openDialog by remember { mutableStateOf(false) }
    val v = studio.version
    Column(Modifier.background(Panel).statusBarsPadding().padding(horizontal = 10.dp, vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).clickable { renameDialog = true }) {
                Text("VEGA SKETCH", color = Gold, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 2.sp)
                Text(studio.sketch.name, color = TextHi, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            AssistChip(onClick = { tempoDialog = true }, label = { Text("${"%.0f".format(studio.sketch.bpm)} BPM", color = TextHi) })
            Spacer(Modifier.width(4.dp))
            IconButton(onClick = { studio.toggleMetronome() }) { Icon(Icons.Filled.AvTimer, "Metronome", tint = if (studio.metronome) Gold else TextDim) }
            val rec = studio.isRecording()
            val blink = (floor(studio.beat() * 2).toInt() % 2 == 0)
            IconButton(onClick = {
                val t = studio.sketch.tracks.getOrNull(studio.selectedTrack)
                if (!rec && t?.type == TrackType.AUDIO) withMic { studio.toggleRecord() } else studio.toggleRecord()
            }) {
                Icon(Icons.Filled.FiberManualRecord, "Record", tint = if (rec && (!studio.isCountingIn() || blink)) RecRed else RecRed.copy(alpha = 0.45f), modifier = Modifier.size(30.dp))
            }
            IconButton(onClick = { studio.togglePlay() }) {
                Icon(if (studio.isPlaying()) Icons.Filled.Stop else Icons.Filled.PlayArrow, "Play", tint = if (studio.isPlaying()) PlayGreen else TextHi, modifier = Modifier.size(32.dp))
            }
            Box {
                IconButton(onClick = { menu = true }) { Icon(Icons.Filled.MoreVert, "Menu", tint = TextHi) }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text("Send to desktop (Quick Share / Drive...)") }, leadingIcon = { Icon(Icons.Filled.Share, null) }, onClick = { menu = false; onExport() })
                    DropdownMenuItem(text = { Text("Save to Downloads") }, leadingIcon = { Icon(Icons.Filled.Download, null) }, onClick = { menu = false; onSaveToDownloads() })
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text("New sketch") }, leadingIcon = { Icon(Icons.Filled.Add, null) }, onClick = { menu = false; studio.newSketch() })
                    DropdownMenuItem(text = { Text("Open sketch...") }, leadingIcon = { Icon(Icons.Filled.FolderOpen, null) }, onClick = { menu = false; openDialog = true })
                    DropdownMenuItem(text = { Text("Rename...") }, leadingIcon = { Icon(Icons.Filled.Edit, null) }, onClick = { menu = false; renameDialog = true })
                    DropdownMenuItem(text = { Text("Undo last clip change") }, leadingIcon = { Icon(Icons.AutoMirrored.Filled.Undo, null) }, onClick = { menu = false; studio.undo() })
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text("Quantise recordings to 1/16") }, trailingIcon = { Checkbox(studio.quantise, null) }, onClick = { studio.quantise = !studio.quantise })
                    DropdownMenuItem(text = { Text("Overdub (record over a clip keeps it)") }, trailingIcon = { Checkbox(studio.overdub, null) }, onClick = { studio.overdub = !studio.overdub })
                }
            }
        }
        // where recording goes
        val t = studio.sketch.tracks.getOrNull(studio.selectedTrack)
        Row(verticalAlignment = Alignment.CenterVertically) {
            val beat = studio.beat()
            val bpb = studio.sketch.beatsPerBar
            val pos = if (studio.isCountingIn()) "COUNT-IN ${(floor(beat).toInt() + bpb + 1)}" else if (studio.isPlaying()) "${floor(beat / bpb).toInt() + 1}.${(floor(beat).toInt() % bpb) + 1}" else "STOPPED"
            Text(pos, color = if (studio.isRecording()) RecRed else Gold, fontSize = 12.sp, fontWeight = FontWeight.Bold, modifier = Modifier.width(96.dp))
            Text(if (t == null) "Add a track to start" else "Rec into: ${t.name} > ${studio.sketch.scenes[studio.selectedScene]}", color = TextDim, fontSize = 12.sp, modifier = Modifier.weight(1f), maxLines = 1)
            Text("Bars:", color = TextDim, fontSize = 12.sp)
            listOf(1, 2, 4, 8).forEach { b ->
                Text(" $b ", color = if (studio.recordBars == b) Navy else TextHi, fontSize = 12.sp, fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(start = 3.dp).clip(RoundedCornerShape(4.dp)).background(if (studio.recordBars == b) Gold else PanelHi).clickable { studio.recordBars = b }.padding(horizontal = 4.dp, vertical = 1.dp))
            }
        }
        v.hashCode()
    }
    if (tempoDialog) TempoDialog(studio) { tempoDialog = false }
    if (renameDialog) TextDialog("Rename sketch", studio.sketch.name, { renameDialog = false }) { studio.rename(it) }
    if (openDialog) OpenDialog(studio) { openDialog = false }
}

@Composable
fun TempoDialog(studio: Studio, onDismiss: () -> Unit) {
    var bpm by remember { mutableFloatStateOf(studio.sketch.bpm.toFloat()) }
    val taps = remember { mutableListOf<Long>() }
    AlertDialog(onDismissRequest = onDismiss, confirmButton = { TextButton(onClick = { studio.setTempo(bpm.toDouble()); onDismiss() }) { Text("OK") } },
        title = { Text("Tempo") },
        text = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("${"%.0f".format(bpm)} BPM", fontSize = 30.sp, color = Gold, fontWeight = FontWeight.Bold)
                Slider(value = bpm, onValueChange = { bpm = it; studio.setTempo(it.toDouble()) }, valueRange = 60f..200f)
                Row {
                    OutlinedButton(onClick = { bpm = (bpm - 1).coerceAtLeast(60f); studio.setTempo(bpm.toDouble()) }) { Text("-1") }
                    Spacer(Modifier.width(8.dp))
                    Button(onClick = {
                        val now = System.currentTimeMillis()
                        if (taps.isNotEmpty() && now - taps.last() > 2000) taps.clear()
                        taps += now
                        if (taps.size >= 3) { val avg = (taps.last() - taps.first()) / (taps.size - 1).toFloat(); bpm = (60000f / avg).coerceIn(60f, 200f); studio.setTempo(bpm.toDouble()) }
                        if (taps.size > 8) taps.removeAt(0)
                    }) { Text("TAP") }
                    Spacer(Modifier.width(8.dp))
                    OutlinedButton(onClick = { bpm = (bpm + 1).coerceAtMost(200f); studio.setTempo(bpm.toDouble()) }) { Text("+1") }
                }
                Spacer(Modifier.height(6.dp))
                Row { listOf(118, 120, 122, 124, 126).forEach { b -> TextButton(onClick = { bpm = b.toFloat(); studio.setTempo(b.toDouble()) }) { Text("$b") } } }
            }
        })
}

@Composable
fun TextDialog(title: String, initial: String, onDismiss: () -> Unit, onOk: (String) -> Unit) {
    var text by remember { mutableStateOf(initial) }
    AlertDialog(onDismissRequest = onDismiss, title = { Text(title) },
        text = { OutlinedTextField(value = text, onValueChange = { text = it }, singleLine = true) },
        confirmButton = { TextButton(onClick = { onOk(text); onDismiss() }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}

@Composable
fun OpenDialog(studio: Studio, onDismiss: () -> Unit) {
    var list by remember { mutableStateOf(studio.store.list()) }
    AlertDialog(onDismissRequest = onDismiss, confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } }, title = { Text("Your sketches") },
        text = {
            Column(Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
                list.forEach { (id, name) ->
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().clickable { studio.openId(id); onDismiss() }.padding(vertical = 6.dp)) {
                        Icon(Icons.Filled.MusicNote, null, tint = if (id == studio.sketch.id) Gold else TextDim)
                        Text(name, Modifier.weight(1f).padding(start = 8.dp), color = TextHi)
                        if (id != studio.sketch.id) IconButton(onClick = { studio.store.delete(id); list = studio.store.list() }) { Icon(Icons.Filled.Delete, "Delete", tint = TextDim) }
                    }
                }
            }
        })
}

// ---------------------------------------------------------------- SONG: the clip launcher
private val cellH = 58.dp
private val colW = 112.dp

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SongScreen(studio: Studio, onEdit: (Tab) -> Unit) {
    val s = studio.sketch
    val hs = rememberScrollState()
    var trackMenu by remember { mutableStateOf(-1) }
    var clipMenu by remember { mutableStateOf<Pair<Int, Int>?>(null) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(Modifier.padding(8.dp)) {
            // scenes
            Column(Modifier.width(74.dp)) {
                Box(Modifier.height(64.dp), contentAlignment = Alignment.BottomStart) { Text("SCENES", color = TextDim, fontSize = 10.sp, fontWeight = FontWeight.Bold) }
                for (sc in 0 until SCENES) {
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.height(cellH).padding(vertical = 2.dp).fillMaxWidth()
                        .clip(RoundedCornerShape(6.dp)).background(if (studio.selectedScene == sc) PanelHi else Panel)
                        .combinedClickable(onClick = { studio.launchScene(sc) }, onLongClick = { studio.selectedScene = sc })) {
                        Icon(Icons.Filled.PlayArrow, null, tint = Gold, modifier = Modifier.size(22.dp))
                        Text(s.scenes[sc], color = TextHi, fontSize = 11.sp, maxLines = 2)
                    }
                }
            }
            Spacer(Modifier.width(6.dp))
            Row(Modifier.horizontalScroll(hs)) {
                s.tracks.forEachIndexed { ti, t ->
                    Column(Modifier.width(colW).padding(horizontal = 2.dp)) {
                        // header
                        Box(Modifier.height(64.dp).fillMaxWidth().clip(RoundedCornerShape(6.dp)).background(if (studio.selectedTrack == ti) PanelHi else Panel)
                            .combinedClickable(onClick = { studio.selectedTrack = ti }, onLongClick = { studio.selectedTrack = ti; trackMenu = ti })) {
                            Box(Modifier.fillMaxWidth().height(4.dp).background(Color(t.colour)))
                            Column(Modifier.padding(6.dp, 8.dp, 6.dp, 4.dp)) {
                                Text(t.name, color = TextHi, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(t.soundName + if (t.mute) "  MUTED" else if (t.solo) "  SOLO" else "", color = if (t.mute) RecRed else TextDim, fontSize = 10.sp, maxLines = 1)
                                LinearProgressIndicator(progress = { studio.level(ti).toFloat().coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth().padding(top = 4.dp).height(3.dp), color = Color(t.colour), trackColor = Navy)
                            }
                            if (trackMenu == ti) TrackMenu(studio, ti, onEdit) { trackMenu = -1 }
                        }
                        for (sc in 0 until SCENES) {
                            val c = t.clips[sc]
                            val playing = studio.playingScene(ti) == sc
                            val queued = studio.queuedScene(ti) == sc
                            val blink = floor(studio.beat() * 4).toInt() % 2 == 0
                            val sel = studio.selectedTrack == ti && studio.selectedScene == sc
                            val col = Color(t.colour)
                            Box(Modifier.height(cellH).padding(vertical = 2.dp).fillMaxWidth().clip(RoundedCornerShape(6.dp))
                                .background(if (c == null) Panel else col.copy(alpha = if (playing) 0.85f else if (queued && blink) 0.6f else 0.32f))
                                .border(if (sel) 2.dp else 0.dp, if (sel) Gold else Color.Transparent, RoundedCornerShape(6.dp))
                                .combinedClickable(
                                    onClick = { studio.selectedTrack = ti; studio.selectedScene = sc; if (c != null) studio.launch(ti, sc) },
                                    onDoubleClick = { studio.selectedTrack = ti; studio.selectedScene = sc; onEdit(editorFor(t)) },
                                    onLongClick = { studio.selectedTrack = ti; studio.selectedScene = sc; clipMenu = ti to sc })) {
                                if (c != null) {
                                    Row(Modifier.padding(6.dp), verticalAlignment = Alignment.CenterVertically) {
                                        Icon(if (playing) Icons.Filled.Pause else Icons.Filled.PlayArrow, null, tint = if (playing) Navy else TextHi, modifier = Modifier.size(18.dp))
                                        Column {
                                            Text(c.name, color = if (playing) Navy else TextHi, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                            Text("${c.bars} bar" + if (c.bars > 1) "s" else "", color = if (playing) Navy.copy(alpha = 0.7f) else TextDim, fontSize = 9.sp)
                                        }
                                    }
                                    if (playing) LinearProgressIndicator(progress = { studio.progress(ti).toFloat() }, modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(3.dp), color = Navy, trackColor = Color.Transparent)
                                } else if (sel) Text("+", color = Gold, fontSize = 20.sp, modifier = Modifier.align(Alignment.Center))
                                if (clipMenu == ti to sc) ClipMenu(studio, ti, sc, onEdit) { clipMenu = null }
                            }
                        }
                        // stop button
                        Box(Modifier.padding(top = 4.dp).height(34.dp).fillMaxWidth().clip(RoundedCornerShape(6.dp)).background(Panel).clickable { NativeEngine.stopTrack(ti) }, contentAlignment = Alignment.Center) {
                            Icon(Icons.Filled.Stop, "Stop", tint = if (studio.playingScene(ti) >= 0) TextHi else TextDim.copy(alpha = 0.4f))
                        }
                    }
                }
                if (s.tracks.size < MAX_TRACKS) Column(Modifier.width(colW).padding(horizontal = 2.dp)) {
                    Spacer(Modifier.height(64.dp))
                    TrackType.values().forEach { type ->
                        OutlinedButton(onClick = { studio.addTrack(type) }, modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                            Text("+ " + type.label.replaceFirstChar { it.uppercase() }, fontSize = 12.sp)
                        }
                    }
                }
            }
        }
        Text("Tap a clip to launch it (it starts on the next bar). Double-tap to edit, long-press for more. Tap an empty slot to select it, then press record.",
            color = TextDim, fontSize = 11.sp, modifier = Modifier.padding(12.dp))
    }
}

fun editorFor(t: Track) = when (t.type) { TrackType.DRUMS -> Tab.BEATS; TrackType.KEYS -> Tab.KEYS; TrackType.AUDIO -> Tab.RECORD }

@Composable
fun TrackMenu(studio: Studio, ti: Int, onEdit: (Tab) -> Unit, onDismiss: () -> Unit) {
    val t = studio.sketch.tracks.getOrNull(ti) ?: return
    var rename by remember { mutableStateOf(false) }
    var volume by remember { mutableStateOf(false) }
    DropdownMenu(expanded = true, onDismissRequest = onDismiss) {
        DropdownMenuItem(text = { Text("Edit") }, onClick = { onDismiss(); onEdit(editorFor(t)) })
        DropdownMenuItem(text = { Text("Rename...") }, onClick = { rename = true })
        DropdownMenuItem(text = { Text("Volume / pan...") }, onClick = { volume = true })
        DropdownMenuItem(text = { Text(if (t.mute) "Unmute" else "Mute") }, onClick = { studio.updateTrack(ti) { mute = !mute }; onDismiss() })
        DropdownMenuItem(text = { Text(if (t.solo) "Unsolo" else "Solo") }, onClick = { studio.updateTrack(ti) { solo = !solo }; onDismiss() })
        if (t.type != TrackType.AUDIO) {
            HorizontalDivider()
            (if (t.type == TrackType.DRUMS) KIT_NAMES else PRESET_NAMES).forEachIndexed { i, n ->
                DropdownMenuItem(text = { Text(n, color = if (t.sound == i) Gold else TextHi) }, onClick = { studio.updateTrack(ti) { sound = i }; onDismiss() })
            }
        }
        HorizontalDivider()
        DropdownMenuItem(text = { Text("Delete track", color = RecRed) }, onClick = { studio.deleteTrack(ti); onDismiss() })
    }
    if (rename) TextDialog("Rename track", t.name, { rename = false; onDismiss() }) { n -> studio.updateTrack(ti) { name = n.ifBlank { name } } }
    if (volume) {
        var vol by remember { mutableFloatStateOf(t.volume) }
        var pan by remember { mutableFloatStateOf(t.pan) }
        AlertDialog(onDismissRequest = { volume = false; onDismiss() }, confirmButton = { TextButton(onClick = { volume = false; onDismiss() }) { Text("Done") } },
            title = { Text(t.name) },
            text = {
                Column {
                    Text("Volume ${(vol * 100).toInt()}%")
                    Slider(vol, { vol = it; studio.updateTrack(ti) { this.volume = it } }, valueRange = 0f..1.2f)
                    Text("Pan " + if (pan < -0.02f) "L ${(-pan * 100).toInt()}" else if (pan > 0.02f) "R ${(pan * 100).toInt()}" else "centre")
                    Slider(pan, { pan = it; studio.updateTrack(ti) { this.pan = it } }, valueRange = -1f..1f)
                }
            })
    }
}

@Composable
fun ClipMenu(studio: Studio, ti: Int, sc: Int, onEdit: (Tab) -> Unit, onDismiss: () -> Unit) {
    val t = studio.sketch.tracks.getOrNull(ti) ?: return
    val c = t.clips[sc]
    var rename by remember { mutableStateOf(false) }
    DropdownMenu(expanded = true, onDismissRequest = onDismiss) {
        if (c == null) {
            if (t.type != TrackType.AUDIO) DropdownMenuItem(text = { Text("New empty clip (${studio.recordBars} bars)") }, onClick = { studio.ensureClip(ti, sc); onDismiss(); onEdit(editorFor(t)) })
            DropdownMenuItem(text = { Text("Record into this slot") }, onClick = { onDismiss(); onEdit(editorFor(t)) })
        } else {
            DropdownMenuItem(text = { Text("Edit") }, onClick = { onDismiss(); onEdit(editorFor(t)) })
            DropdownMenuItem(text = { Text("Rename...") }, onClick = { rename = true })
            if (t.type != TrackType.AUDIO) {
                HorizontalDivider()
                listOf(1, 2, 4, 8).forEach { b -> DropdownMenuItem(text = { Text("$b bar" + if (b > 1) "s" else "", color = if (c.bars == b) Gold else TextHi) }, onClick = { studio.setClipBars(ti, sc, b); onDismiss() }) }
                DropdownMenuItem(text = { Text("Quantise to 1/16") }, onClick = { studio.quantiseClip(ti, sc); onDismiss() })
            }
            HorizontalDivider()
            DropdownMenuItem(text = { Text("Duplicate to the next empty slot") }, onClick = { studio.duplicateClip(ti, sc); onDismiss() })
            DropdownMenuItem(text = { Text("Delete clip", color = RecRed) }, onClick = { studio.deleteClip(ti, sc); onDismiss() })
        }
    }
    if (rename && c != null) TextDialog("Rename clip", c.name, { rename = false; onDismiss() }) { n -> c.name = n.ifBlank { c.name }; studio.changed() }
}

// ---------------------------------------------------------------- shared: pick a track of a type + the slot
@Composable
fun TrackAndSlotBar(studio: Studio, type: TrackType): Int? {
    val s = studio.sketch
    val indices = s.tracks.indices.filter { s.tracks[it].type == type }
    if (indices.isEmpty()) {
        Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("No ${type.label} track yet", color = TextHi, fontSize = 16.sp)
            Spacer(Modifier.height(8.dp))
            Button(onClick = { studio.addTrack(type) }) { Text("Add a ${type.label} track") }
        }
        return null
    }
    if (studio.selectedTrack !in indices) studio.selectedTrack = indices.first()
    val ti = studio.selectedTrack
    Column(Modifier.background(Panel).padding(horizontal = 8.dp, vertical = 4.dp)) {
        Row(Modifier.horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
            indices.forEach { i ->
                FilterChip(selected = i == ti, onClick = { studio.selectedTrack = i }, label = { Text(s.tracks[i].name) },
                    leadingIcon = { Box(Modifier.size(10.dp).clip(CircleShape).background(Color(s.tracks[i].colour))) }, modifier = Modifier.padding(end = 6.dp))
            }
        }
        Row(Modifier.horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
            Text("SLOT", color = TextDim, fontSize = 10.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(end = 6.dp))
            for (sc in 0 until SCENES) {
                val c = s.tracks[ti].clips[sc]
                val playing = studio.playingScene(ti) == sc
                Box(Modifier.padding(end = 4.dp).size(34.dp, 28.dp).clip(RoundedCornerShape(5.dp))
                    .background(if (studio.selectedScene == sc) Gold else if (c != null) Color(s.tracks[ti].colour).copy(alpha = if (playing) 0.9f else 0.35f) else PanelHi)
                    .clickable { studio.selectedScene = sc }, contentAlignment = Alignment.Center) {
                    Text("${sc + 1}", color = if (studio.selectedScene == sc) Navy else TextHi, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                }
            }
            val c = s.tracks[ti].clips[studio.selectedScene]
            IconButton(onClick = { if (c != null) studio.launch(ti, studio.selectedScene) }, enabled = c != null) {
                Icon(if (studio.playingScene(ti) == studio.selectedScene) Icons.Filled.Pause else Icons.Filled.PlayArrow, "Play slot", tint = if (c != null) Gold else TextDim)
            }
        }
    }
    return ti
}

// ---------------------------------------------------------------- BEATS: pads + step sequencer
@Composable
fun BeatsScreen(studio: Studio) {
    val v = studio.version
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        val ti = TrackAndSlotBar(studio, TrackType.DRUMS) ?: return@Column
        val t = studio.sketch.tracks[ti]
        val sc = studio.selectedScene
        val clip = t.clips[sc]
        var selectedPad by remember { mutableIntStateOf(36) }
        var page by remember { mutableIntStateOf(0) }
        val bars = clip?.bars ?: 1
        if (page >= bars) page = 0
        val lit = remember { mutableStateMapOf<Int, Boolean>() }
        // kit
        Row(Modifier.horizontalScroll(rememberScrollState()).padding(8.dp, 6.dp)) {
            KIT_NAMES.forEachIndexed { i, n ->
                FilterChip(selected = t.sound == i, onClick = { studio.updateTrack(ti) { sound = i } }, label = { Text(n, fontSize = 12.sp) }, modifier = Modifier.padding(end = 6.dp))
            }
        }
        // pads
        Column(Modifier.padding(horizontal = 8.dp)) {
            for (row in 0 until 4) Row {
                for (col in 0 until 4) {
                    val note = PAD_NOTES[row * 4 + col]
                    val on = lit[note] == true
                    val sel = note == selectedPad
                    Box(Modifier.weight(1f).aspectRatio(1.15f).padding(3.dp).clip(RoundedCornerShape(10.dp))
                        .background(if (on) Gold else Color(t.colour).copy(alpha = if (sel) 0.55f else 0.25f))
                        .border(if (sel) 2.dp else 0.dp, if (sel) Gold else Color.Transparent, RoundedCornerShape(10.dp))
                        .pointerInput(note, ti) {
                            detectTapGestures(onPress = { pos ->
                                // harder near the top of the pad
                                val vel = (127 - (pos.y / size.height) * 60).toInt().coerceIn(60, 127)
                                studio.noteOn(ti, note, vel); lit[note] = true; selectedPad = note
                                tryAwaitRelease()
                                studio.noteOff(ti, note); lit[note] = false
                            })
                        }, contentAlignment = Alignment.BottomStart) {
                        Text(PAD_NAMES[note] ?: "", color = if (on) Navy else TextHi, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(6.dp))
                    }
                }
            }
        }
        // steps for the selected pad
        val bpb = studio.sketch.beatsPerBar
        val stepsPerBar = bpb * 4
        val playingHere = studio.playingScene(ti) == sc
        val nowStep = if (playingHere) floor(studio.progress(ti) * bars * stepsPerBar).toInt() else -1
        Row(Modifier.padding(start = 10.dp, end = 10.dp, top = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("STEPS: ${PAD_NAMES[selectedPad]}", color = Gold, fontSize = 12.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            if (bars > 1) for (p in 0 until bars) {
                Text(" ${p + 1} ", color = if (p == page) Navy else TextHi, fontSize = 12.sp, modifier = Modifier.padding(start = 4.dp).clip(RoundedCornerShape(4.dp))
                    .background(if (p == page) Gold else PanelHi).clickable { page = p }.padding(horizontal = 6.dp, vertical = 2.dp))
            }
            TextButton(onClick = { studio.clearNotes(ti, sc) }, enabled = clip != null) { Text("CLEAR") }
        }
        for (half in 0 until stepsPerBar / 8) Row(Modifier.padding(horizontal = 8.dp)) {
            for (k in 0 until 8) {
                val step = page * stepsPerBar + half * 8 + k
                val on = studio.hasStep(clip, selectedPad, step)
                Box(Modifier.weight(1f).height(44.dp).padding(2.dp).clip(RoundedCornerShape(6.dp))
                    .background(if (on) Color(t.colour) else if ((half * 8 + k) % 4 == 0) PanelHi else Panel)
                    .border(if (step == nowStep) 2.dp else 0.dp, if (step == nowStep) TextHi else Color.Transparent, RoundedCornerShape(6.dp))
                    .clickable { studio.toggleStep(ti, sc, selectedPad, step) })
            }
        }
        // overview: every sound on this page
        Text("ALL SOUNDS", color = TextDim, fontSize = 10.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(start = 10.dp, top = 10.dp))
        Column(Modifier.padding(horizontal = 8.dp)) {
            PAD_NOTES.forEach { note ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(PAD_NAMES[note] ?: "", color = if (note == selectedPad) Gold else TextDim, fontSize = 9.sp, modifier = Modifier.width(62.dp).clickable { selectedPad = note })
                    for (k in 0 until stepsPerBar) {
                        val step = page * stepsPerBar + k
                        val on = studio.hasStep(clip, note, step)
                        Box(Modifier.weight(1f).height(14.dp).padding(1.dp).background(if (on) Color(t.colour) else if (step == nowStep) TextDim.copy(alpha = 0.4f) else if (k % 4 == 0) PanelHi else Panel)
                            .clickable { studio.toggleStep(ti, sc, note, step) })
                    }
                }
            }
        }
        Text("Hit the pads to play. Press record (top) to record your playing into the selected slot - it loops and adds each pass. Or tap the steps.",
            color = TextDim, fontSize = 11.sp, modifier = Modifier.padding(12.dp))
        v.hashCode()
    }
}

// ---------------------------------------------------------------- KEYS: chord pads + keyboard
val NOTE_NAMES = listOf("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B")
enum class Scale(val label: String, val steps: List<Int>) {
    MAJOR("Major", listOf(0, 2, 4, 5, 7, 9, 11)), MINOR("Minor", listOf(0, 2, 3, 5, 7, 8, 10)), DORIAN("Dorian", listOf(0, 2, 3, 5, 7, 9, 10))
}

/** Diatonic chords of the key: stacked thirds, 7ths (and 9ths when "rich"), voiced around middle C */
fun chordFor(root: Int, scale: Scale, degree: Int, rich: Boolean, octave: Int): List<Int> {
    val st = scale.steps
    fun deg(d: Int) = st[d % 7] + 12 * (d / 7)
    val tones = mutableListOf(0, 2, 4, 6).apply { if (rich) add(8) }.map { deg(degree + it) }
    val base = 48 + 12 * octave + root
    return tones.map { base + it }
}
fun chordName(root: Int, scale: Scale, degree: Int, rich: Boolean): String {
    val notes = chordFor(root, scale, degree, false, 0).map { it - chordFor(root, scale, degree, false, 0)[0] }
    val third = notes[1]; val fifth = notes[2]; val seventh = notes[3]
    val q = when {
        third == 4 && seventh == 11 -> if (rich) "maj9" else "maj7"
        third == 4 && seventh == 10 -> if (rich) "9" else "7"
        third == 3 && fifth == 6 -> "m7b5"
        third == 3 -> if (rich) "m9" else "m7"
        else -> ""
    }
    return NOTE_NAMES[(root + scale.steps[degree]) % 12] + q
}

@Composable
fun KeysScreen(studio: Studio) {
    val v = studio.version
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        val ti = TrackAndSlotBar(studio, TrackType.KEYS) ?: return@Column
        val t = studio.sketch.tracks[ti]
        val sc = studio.selectedScene
        var root by remember { mutableIntStateOf(9) }          // A minor / C major family: good soulful default
        var scale by remember { mutableStateOf(Scale.MINOR) }
        var octave by remember { mutableIntStateOf(0) }
        var rich by remember { mutableStateOf(true) }
        var bass by remember { mutableStateOf(true) }
        Row(Modifier.horizontalScroll(rememberScrollState()).padding(8.dp, 6.dp)) {
            PRESET_NAMES.forEachIndexed { i, n -> FilterChip(selected = t.sound == i, onClick = { studio.updateTrack(ti) { sound = i } }, label = { Text(n, fontSize = 12.sp) }, modifier = Modifier.padding(end = 6.dp)) }
        }
        Row(Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            var keyMenu by remember { mutableStateOf(false) }
            Box {
                OutlinedButton(onClick = { keyMenu = true }) { Text("Key: ${NOTE_NAMES[root]} ${scale.label}") }
                DropdownMenu(keyMenu, { keyMenu = false }) {
                    Scale.values().forEach { s -> DropdownMenuItem(text = { Text(s.label, color = if (s == scale) Gold else TextHi) }, onClick = { scale = s }) }
                    HorizontalDivider()
                    NOTE_NAMES.forEachIndexed { i, n -> DropdownMenuItem(text = { Text(n, color = if (i == root) Gold else TextHi) }, onClick = { root = i; keyMenu = false }) }
                }
            }
            Spacer(Modifier.width(6.dp))
            FilterChip(rich, { rich = !rich }, label = { Text("9ths") })
            Spacer(Modifier.width(6.dp))
            FilterChip(bass, { bass = !bass }, label = { Text("Bass note") })
            Spacer(Modifier.width(6.dp))
            IconButton(onClick = { octave = (octave - 1).coerceAtLeast(-2) }) { Icon(Icons.Filled.Remove, "Octave down", tint = TextHi) }
            Text("Oct ${if (octave >= 0) "+" else ""}$octave", color = TextHi, fontSize = 12.sp)
            IconButton(onClick = { octave = (octave + 1).coerceAtMost(2) }) { Icon(Icons.Filled.Add, "Octave up", tint = TextHi) }
        }
        // chord pads: I..vii and the I an octave up
        Column(Modifier.padding(horizontal = 8.dp)) {
            for (row in 0 until 2) Row {
                for (col in 0 until 4) {
                    val degree = row * 4 + col
                    val d = degree % 7
                    val label = if (degree == 7) chordName(root, scale, 0, rich) + " (hi)" else chordName(root, scale, d, rich)
                    var down by remember { mutableStateOf(false) }
                    Box(Modifier.weight(1f).height(64.dp).padding(3.dp).clip(RoundedCornerShape(10.dp)).background(if (down) Gold else Color(t.colour).copy(alpha = 0.3f))
                        .pointerInput(root, scale, rich, bass, octave, ti, degree) {
                            detectTapGestures(onPress = {
                                val notes = chordFor(root, scale, d, rich, octave + if (degree == 7) 1 else 0).toMutableList()
                                if (bass) notes += 36 + 12 * octave + root + scale.steps[d]
                                notes.forEach { studio.noteOn(ti, it, 96) }; down = true
                                tryAwaitRelease()
                                notes.forEach { studio.noteOff(ti, it) }; down = false
                            })
                        }, contentAlignment = Alignment.Center) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text(listOf("I", "II", "III", "IV", "V", "VI", "VII", "I")[degree], color = if (down) Navy else TextDim, fontSize = 10.sp)
                            Text(label, color = if (down) Navy else TextHi, fontSize = 14.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
                        }
                    }
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        PianoKeyboard(lowNote = 48 + 12 * octave, octaves = 2, scaleRoot = root, scale = scale, colour = Color(t.colour),
            onOn = { studio.noteOn(ti, it, 100) }, onOff = { studio.noteOff(ti, it) })
        // the clip
        val clip = t.clips[sc]
        Row(Modifier.padding(start = 10.dp, end = 4.dp, top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(clip?.let { "${it.name}  -  ${it.notes.size} notes, ${it.bars} bars" } ?: "Empty slot - press record (top) and play", color = TextDim, fontSize = 12.sp, modifier = Modifier.weight(1f))
            TextButton(onClick = { studio.quantiseClip(ti, sc) }, enabled = clip != null) { Text("QUANTISE") }
            TextButton(onClick = { studio.clearNotes(ti, sc) }, enabled = clip != null) { Text("CLEAR") }
            TextButton(onClick = { studio.undo() }) { Text("UNDO") }
        }
        MiniRoll(clip, studio.sketch.beatsPerBar, if (studio.playingScene(ti) == sc) studio.progress(ti) else -1.0, Color(t.colour))
        v.hashCode()
    }
}

@Composable
fun PianoKeyboard(lowNote: Int, octaves: Int, scaleRoot: Int, scale: Scale, colour: Color, onOn: (Int) -> Unit, onOff: (Int) -> Unit) {
    var size by remember { mutableStateOf(IntSize.Zero) }
    val down = remember { mutableStateMapOf<Int, Boolean>() }
    val on by rememberUpdatedState(onOn)
    val off by rememberUpdatedState(onOff)
    val whites = listOf(0, 2, 4, 5, 7, 9, 11)
    val nWhite = octaves * 7 + 1
    fun keyAt(p: Offset): Int? {
        if (size.width == 0) return null
        val ww = size.width / nWhite.toFloat()
        if (p.y < size.height * 0.6f) {   // black keys first
            for (o in 0 until octaves) for ((wi, semi) in listOf(0 to 1, 1 to 3, 3 to 6, 4 to 8, 5 to 10)) {
                val x = (o * 7 + wi + 1) * ww
                if (p.x > x - ww * 0.3f && p.x < x + ww * 0.3f) return lowNote + o * 12 + semi
            }
        }
        val idx = (p.x / ww).toInt().coerceIn(0, nWhite - 1)
        return lowNote + (idx / 7) * 12 + whites[idx % 7]
    }
    Canvas(Modifier.fillMaxWidth().height(170.dp).padding(horizontal = 6.dp).onSizeChanged { size = it }
        .pointerInput(lowNote) {
            awaitPointerEventScope {
                val held = mutableMapOf<PointerId, Int>()
                while (true) {
                    val ev = awaitPointerEvent()
                    for (ch in ev.changes) {
                        val prev = held[ch.id]
                        if (ch.pressed) {
                            val k = keyAt(ch.position)
                            if (k != prev) {
                                if (prev != null) { off(prev); down.remove(prev) }
                                if (k != null) { on(k); down[k] = true; held[ch.id] = k } else held.remove(ch.id)
                            }
                        } else if (prev != null) { off(prev); down.remove(prev); held.remove(ch.id) }
                        ch.consume()
                    }
                }
            }
        }) {
        val ww = this.size.width / nWhite
        for (i in 0 until nWhite) {
            val note = lowNote + (i / 7) * 12 + whites[i % 7]
            val inScale = scale.steps.contains(((note - scaleRoot) % 12 + 12) % 12)
            drawRoundRect(if (down[note] == true) colour else if (inScale) Color(0xFFF2F2F2) else Color(0xFFBDBDC4), Offset(i * ww + 1, 0f), Size(ww - 2, this.size.height),
                androidx.compose.ui.geometry.CornerRadius(6f, 6f))
            if ((note % 12) == 0) drawCircle(GoldDim, 4f, Offset(i * ww + ww / 2, this.size.height - 14f))
        }
        for (o in 0 until octaves) for ((wi, semi) in listOf(0 to 1, 1 to 3, 3 to 6, 4 to 8, 5 to 10)) {
            val note = lowNote + o * 12 + semi
            val x = (o * 7 + wi + 1) * ww
            drawRoundRect(if (down[note] == true) colour else Color(0xFF1A1A1F), Offset(x - ww * 0.3f, 0f), Size(ww * 0.6f, this.size.height * 0.6f),
                androidx.compose.ui.geometry.CornerRadius(5f, 5f))
        }
    }
}

@Composable
fun MiniRoll(clip: Clip?, bpb: Int, progress: Double, colour: Color) {
    Canvas(Modifier.fillMaxWidth().height(120.dp).padding(8.dp).clip(RoundedCornerShape(8.dp)).background(Panel)) {
        if (clip == null) return@Canvas
        val len = (clip.bars * bpb).toDouble()
        for (b in 0..(clip.bars * bpb)) {
            val x = (b / len * size.width).toFloat()
            drawLine(if (b % bpb == 0) TextDim.copy(alpha = 0.5f) else TextDim.copy(alpha = 0.15f), Offset(x, 0f), Offset(x, size.height))
        }
        if (clip.notes.isEmpty()) return@Canvas
        val lo = clip.notes.minOf { it.note } - 1; val hi = clip.notes.maxOf { it.note } + 1
        val rowH = size.height / (hi - lo + 1)
        clip.notes.forEach { n ->
            val x = (n.start / len * size.width).toFloat(); val w = (n.length / len * size.width).toFloat().coerceAtLeast(3f)
            drawRoundRect(colour.copy(alpha = 0.4f + 0.6f * n.velocity / 127f), Offset(x, size.height - (n.note - lo + 1) * rowH), Size(w, rowH.coerceAtLeast(2f) - 1f),
                androidx.compose.ui.geometry.CornerRadius(2f, 2f))
        }
        if (progress >= 0) { val x = (progress * size.width).toFloat(); drawLine(TextHi, Offset(x, 0f), Offset(x, size.height), 2f) }
    }
}

// ---------------------------------------------------------------- RECORD: audio from the mic
@Composable
fun RecordScreen(studio: Studio, withMic: (() -> Unit) -> Unit) {
    val v = studio.version
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        val ti = TrackAndSlotBar(studio, TrackType.AUDIO) ?: return@Column
        val t = studio.sketch.tracks[ti]
        val sc = studio.selectedScene
        val rec = studio.isRecording()
        Column(Modifier.fillMaxWidth().padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(when {
                rec && studio.isCountingIn() -> "Count-in..."
                rec -> "Recording ${studio.recordBars} bar" + (if (studio.recordBars > 1) "s" else "") + " into slot ${sc + 1}"
                t.clips[sc] != null -> "Slot ${sc + 1} has a take - recording again replaces it"
                else -> "Records ${studio.recordBars} bar" + (if (studio.recordBars > 1) "s" else "") + " at ${"%.0f".format(studio.sketch.bpm)} BPM after a 1-bar count-in"
            }, color = if (rec) RecRed else TextHi, fontSize = 14.sp, textAlign = TextAlign.Center)
            Spacer(Modifier.height(14.dp))
            Box(Modifier.size(132.dp).clip(CircleShape).background(if (rec) RecRed else RecRed.copy(alpha = 0.25f)).border(3.dp, RecRed, CircleShape)
                .clickable { if (rec) studio.stopRecording() else withMic { studio.startRecording() } }, contentAlignment = Alignment.Center) {
                Icon(if (rec) Icons.Filled.Stop else Icons.Filled.Mic, null, tint = TextHi, modifier = Modifier.size(56.dp))
            }
            Spacer(Modifier.height(14.dp))
            Text("INPUT", color = TextDim, fontSize = 10.sp)
            LinearProgressIndicator(progress = { studio.inputLevel().toFloat().coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(0.7f).height(8.dp).clip(RoundedCornerShape(4.dp)),
                color = if (studio.inputLevel() > 0.9) RecRed else PlayGreen, trackColor = PanelHi)
            Text("Use headphones so the beat doesn't get into the recording.", color = TextDim, fontSize = 11.sp, modifier = Modifier.padding(top = 8.dp))
        }
        Text("TAKES ON ${t.name.uppercase()}", color = Gold, fontSize = 11.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(start = 12.dp, top = 6.dp))
        for (s in 0 until SCENES) {
            val c = t.clips[s] ?: continue
            Row(Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 3.dp).clip(RoundedCornerShape(8.dp)).background(if (s == sc) PanelHi else Panel).padding(6.dp),
                verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { studio.launch(ti, s) }) { Icon(if (studio.playingScene(ti) == s) Icons.Filled.Pause else Icons.Filled.PlayArrow, null, tint = Gold) }
                Column(Modifier.weight(1f).clickable { studio.selectedScene = s }) {
                    Text(c.name, color = TextHi, fontSize = 13.sp)
                    Text("Slot ${s + 1}  -  ${c.bars} bars", color = TextDim, fontSize = 11.sp)
                }
                IconButton(onClick = { studio.deleteClip(ti, s) }) { Icon(Icons.Filled.Delete, "Delete", tint = TextDim) }
            }
        }
        v.hashCode()
    }
}

