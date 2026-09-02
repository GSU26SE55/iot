package com.solarbms.setup.v2.ui

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Router
import androidx.compose.material.icons.filled.SystemUpdateAlt
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Snackbar
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.solarbms.setup.v2.EnvSettings
import com.solarbms.setup.v2.Environment
import com.solarbms.setup.v2.SetupViewModel
import com.solarbms.setup.v2.Step
import com.solarbms.setup.v2.UiState
import com.solarbms.setup.v2.WifiNetwork

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SetupApp(
    viewModel: SetupViewModel,
    state: UiState,
    onOpenWifiPanel: () -> Unit,
    onScanQr: () -> Unit,
    onPickFirmware: () -> Unit,
) {
    var showSettings by remember { mutableStateOf(false) }
    var showFirmware by remember { mutableStateOf(false) }
    var showMaintenance by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Solars Gateway", maxLines = 1) },
                actions = {
                    AssistChip(
                        onClick = { showSettings = true },
                        label = { Text(state.env.environment.shortLabel) },
                        colors = AssistChipDefaults.assistChipColors(
                            labelColor = MaterialTheme.colorScheme.primary,
                        ),
                    )
                    IconButton(onClick = viewModel::reload) {
                        Icon(Icons.Default.Refresh, contentDescription = "Reload")
                    }
                    IconButton(
                        onClick = { showMaintenance = true },
                        // Đồng bộ và ghép mới chỉ có nghĩa khi thiết bị đã ghép rồi — giống
                        // trang web chỉ hiện hai mục này khi có mã IoT.
                        enabled = state.alreadyPaired,
                    ) {
                        Icon(Icons.Default.Build, contentDescription = "Device maintenance")
                    }
                    IconButton(
                        onClick = { showFirmware = true },
                        enabled = state.connected,
                    ) {
                        Icon(Icons.Default.SystemUpdateAlt, contentDescription = "Flash firmware")
                    }
                },
            )
        },
        snackbarHost = {
            state.error?.let { message ->
                Snackbar(
                    modifier = Modifier.padding(12.dp),
                    action = { TextButton(onClick = viewModel::dismissError) { Text("Dismiss") } },
                ) { Text(message) }
            }
        },
    ) { padding ->
        Column(Modifier.padding(padding).fillMaxSize()) {
            // Thanh bước chỉ có Ở ĐÂY. Bản cũ vẽ một thanh native rồi trang web trong WebView
            // vẽ thêm một thanh nữa — đúng chỗ "thừa thải" cần dẹp.
            if (state.step != Step.SAVING && state.step != Step.SUCCESS) {
                StepBar(state.step)
            }
            when (state.step) {
                Step.CONNECTING -> ConnectingScreen(state, onOpenWifiPanel, viewModel::discover)
                Step.WIFI -> WifiScreen(viewModel, state)
                Step.QR -> QrScreen(viewModel, state, onScanQr)
                Step.SAVING -> SavingScreen()
                Step.SUCCESS -> SuccessScreen(state, viewModel::restart)
            }
        }
    }

    if (showSettings) {
        EnvSheet(
            state = state,
            onSelect = viewModel::selectEnvironment,
            onUpdate = viewModel::updateEnv,
            onApply = viewModel::applyEnvironmentToGateway,
            onDismiss = { showSettings = false },
        )
    }
    if (showMaintenance) {
        MaintenanceSheet(
            viewModel = viewModel,
            state = state,
            onDismiss = { showMaintenance = false },
        )
    }
    if (showFirmware) {
        FirmwareSheet(
            viewModel = viewModel,
            state = state,
            onPickFile = onPickFirmware,
            onDismiss = { showFirmware = false },
        )
    }
}

// ---- Bước 1: chờ nối SoftAP ----

@Composable
private fun ConnectingScreen(state: UiState, onOpenWifiPanel: () -> Unit, onRetry: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(
            Icons.Default.Router,
            contentDescription = null,
            modifier = Modifier.size(72.dp),
            tint = MaterialTheme.colorScheme.primary,
        )
        Spacer(Modifier.height(20.dp))
        Text("Connect to Solars Gateway", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))
        Text(
            "Open Wi-Fi settings, join SolarGW-xxxx with password 12345678, then come back. " +
                "Stay connected even if Android warns the network has no internet.",
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(28.dp))
        if (state.searching) {
            CircularProgressIndicator()
            Spacer(Modifier.height(12.dp))
            Text("Looking for the gateway…", style = MaterialTheme.typography.bodySmall)
        } else {
            Button(onClick = onOpenWifiPanel, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.Wifi, contentDescription = null)
                Spacer(Modifier.size(8.dp))
                Text("Open Wi-Fi and connect")
            }
            Spacer(Modifier.height(8.dp))
            OutlinedButton(onClick = onRetry, modifier = Modifier.fillMaxWidth()) {
                Text("I am connected — retry")
            }
        }
    }
}

// ---- Bước 2: chọn Wi-Fi nhà ----

@Composable
private fun WifiScreen(viewModel: SetupViewModel, state: UiState) {
    var showPassword by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
        // Cảnh báo lệch môi trường KHÔNG nằm ở đây nữa — nó chỉ hiện trong bảng Environment.
        // Đặt ngay giữa luồng setup thì nó chắn mất bước đang làm, mà việc xử lý lại nằm ở
        // chỗ khác; gom cả cảnh báo lẫn nút sửa vào cùng một chỗ là đúng hơn.
        Text("Home Wi-Fi", style = MaterialTheme.typography.titleMedium)
        Text(
            "The gateway uses this network to send data. 2.4 GHz only.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(8.dp))

        if (state.scanning) {
            LinearProgressIndicator(Modifier.fillMaxWidth())
        } else {
            HorizontalDivider()
        }

        LazyColumn(Modifier.weight(1f)) {
            items(state.networks, key = { it.ssid }) { network ->
                NetworkRow(network, selected = network.ssid == state.ssid) {
                    viewModel.selectNetwork(network.ssid)
                }
            }
        }

        OutlinedTextField(
            value = state.ssid,
            onValueChange = viewModel::setSsid,
            label = { Text("Network name (SSID)") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = state.password,
            onValueChange = viewModel::setPassword,
            label = { Text("Wi-Fi password") },
            singleLine = true,
            visualTransformation = if (showPassword) VisualTransformation.None
            else PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
            trailingIcon = {
                IconButton(onClick = { showPassword = !showPassword }) {
                    Icon(
                        if (showPassword) Icons.Default.VisibilityOff else Icons.Default.Visibility,
                        contentDescription = if (showPassword) "Hide password" else "Show password",
                    )
                }
            },
            modifier = Modifier.fillMaxWidth(),
        )

        state.status?.let {
            Spacer(Modifier.height(8.dp))
            Text(it, style = MaterialTheme.typography.bodySmall)
        }

        Spacer(Modifier.height(12.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(
                onClick = viewModel::refreshScan,
                enabled = !state.scanning && !state.busy,
            ) { Text("Rescan") }
            Button(
                onClick = viewModel::continueFromWifi,
                enabled = !state.busy,
                modifier = Modifier.weight(1f),
            ) {
                if (state.busy) {
                    CircularProgressIndicator(
                        Modifier.size(18.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.onPrimary,
                    )
                    Spacer(Modifier.size(8.dp))
                }
                Text(if (state.alreadyPaired) "Check and save" else "Continue")
            }
        }
        Spacer(Modifier.height(12.dp))
    }
}

@Composable
private fun NetworkRow(network: WifiNetwork, selected: Boolean, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(network.ssid) },
        supportingContent = { Text("${network.rssi} dBm") },
        leadingContent = {
            Icon(
                if (network.secure) Icons.Default.Lock else Icons.Default.Wifi,
                contentDescription = if (network.secure) "Secured" else "Open network",
            )
        },
        colors = if (selected) {
            ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.primaryContainer)
        } else {
            ListItemDefaults.colors()
        },
        // ListItem không tự nhận click — cả hàng bấm được là nhờ modifier này.
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
    )
    HorizontalDivider()
}

// ---- Bước 3: quét QR ----

@Composable
private fun QrScreen(viewModel: SetupViewModel, state: UiState, onScanQr: () -> Unit) {
    var pasted by remember { mutableStateOf("") }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("Pair device", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))
        Text(
            state.status ?: "Scan the device QR from the Admin web app to pair.",
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(24.dp))
        Button(onClick = onScanQr, modifier = Modifier.fillMaxWidth()) { Text("Scan QR") }
        Spacer(Modifier.height(20.dp))
        Text(
            "Or paste the QR text if the camera cannot read it",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = pasted,
            onValueChange = { pasted = it },
            label = { Text("QR URL or text") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        OutlinedButton(
            onClick = { viewModel.applyQr(pasted) },
            enabled = pasted.isNotBlank(),
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Pair with pasted text") }
        Spacer(Modifier.height(24.dp))
        TextButton(onClick = viewModel::backToWifi) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null)
            Spacer(Modifier.size(6.dp))
            Text("Back to Wi-Fi")
        }
    }
}

// ---- Thanh 3 bước ----

@Composable
private fun StepBar(current: Step) {
    val steps = listOf("Gateway", "Home Wi-Fi", "Scan QR")
    val activeIndex = when (current) {
        Step.CONNECTING -> 0
        Step.WIFI -> 1
        else -> 2
    }
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        steps.forEachIndexed { index, label ->
            if (index > 0) {
                HorizontalDivider(
                    modifier = Modifier.weight(1f).padding(horizontal = 6.dp),
                    color = if (index <= activeIndex) MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.outlineVariant,
                )
            }
            StepDot(index + 1, label, index, activeIndex)
        }
    }
    HorizontalDivider()
}

@Composable
private fun StepDot(number: Int, label: String, index: Int, activeIndex: Int) {
    val done = index < activeIndex
    val active = index == activeIndex
    val container = when {
        done || active -> MaterialTheme.colorScheme.primary
        else -> MaterialTheme.colorScheme.surfaceVariant
    }
    val onContainer = when {
        done || active -> MaterialTheme.colorScheme.onPrimary
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier.size(24.dp).clip(CircleShape).background(container),
            contentAlignment = Alignment.Center,
        ) {
            if (done) {
                Icon(
                    Icons.Default.Check,
                    contentDescription = null,
                    tint = onContainer,
                    modifier = Modifier.size(15.dp),
                )
            } else {
                Text(
                    number.toString(),
                    style = MaterialTheme.typography.labelMedium,
                    color = onContainer,
                )
            }
        }
        Spacer(Modifier.size(6.dp))
        Text(
            label,
            style = MaterialTheme.typography.labelMedium,
            color = if (active) MaterialTheme.colorScheme.primary
            else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

// ---- Bước 4: đang lưu ----

@Composable
private fun SavingScreen() {
    Column(
        Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CircularProgressIndicator()
        Spacer(Modifier.height(16.dp))
        Text("Saving configuration…", style = MaterialTheme.typography.titleMedium)
        Text(
            "Keep the ESP32 powered.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

// ---- Bước 5: xong ----

@Composable
private fun SuccessScreen(state: UiState, onDone: () -> Unit) {
    Column(
        Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        SuccessCheck()
        Spacer(Modifier.height(20.dp))
        Text(state.successTitle, style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))
        Text(
            state.successText,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (state.successShowSummary) {
            Spacer(Modifier.height(20.dp))
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    SummaryRow("Device code", state.savedDeviceCode)
                    SummaryRow("Environment", state.env.environment.label)
                    SummaryRow("Backend", state.env.backendUrl)
                    SummaryRow(
                        "MQTT",
                        "${state.env.mqttHost}:${state.env.mqttPort}" +
                            if (state.env.useTls) " (TLS)" else " (plain)",
                    )
                }
            }
        }
        Spacer(Modifier.height(24.dp))
        Button(onClick = onDone, modifier = Modifier.fillMaxWidth()) { Text("Xong") }
    }
}

@Composable
private fun SummaryRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Text(
            label,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(0.35f),
        )
        Text(
            value.ifEmpty { "—" },
            style = MaterialTheme.typography.bodySmall,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.weight(0.65f),
        )
    }
}

/**
 * Tích xanh chạy ĐÚNG MỘT LẦN rồi đứng yên.
 *
 * Đây là chỗ bản cũ hỏng: trang web trong firmware đổi tiêu đề thành "Đã lưu thành công"
 * nhưng để nguyên `animation:spin ... infinite`, nên chữ báo xong mà spinner vẫn quay mãi.
 * `animateFloatAsState` chạy tới đích rồi dừng — KHÔNG dùng `infiniteRepeatable` ở đây.
 */
@Composable
private fun SuccessCheck() {
    var landed by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { landed = true }
    val scale by animateFloatAsState(
        targetValue = if (landed) 1f else 0.6f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessLow,
        ),
        label = "checkScale",
    )
    Icon(
        Icons.Rounded.CheckCircle,
        contentDescription = null,
        tint = MaterialTheme.colorScheme.primary,
        modifier = Modifier.size(96.dp).scale(scale),
    )
}

// ---- Cảnh báo lệch môi trường ----

@Composable
private fun EnvMismatchCard(state: UiState, onApply: () -> Unit) {
    val gateway = state.gateway ?: return
    Card(
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.errorContainer,
            contentColor = MaterialTheme.colorScheme.onErrorContainer,
        ),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text("Environment mismatch", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(
                "This paired gateway points at MQTT ${gateway.mqttHost}:${gateway.mqttPort} " +
                    (if (gateway.mqttUseTls) "(TLS)" else "(plain)") +
                    ", while the app is set to ${state.env.environment.label}.",
                style = MaterialTheme.typography.bodySmall,
            )
            Spacer(Modifier.height(8.dp))
            OutlinedButton(onClick = onApply) {
                Text("Apply ${state.env.environment.label} to gateway")
            }
        }
    }
}

// ---- Cài đặt môi trường ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun EnvSheet(
    state: UiState,
    onSelect: (Environment) -> Unit,
    onUpdate: (EnvSettings) -> Unit,
    onApply: () -> Unit,
    onDismiss: () -> Unit,
) {
    val settings = state.env
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 24.dp).padding(bottom = 32.dp)) {
            Text("Environment", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(4.dp))
            Text(
                "Sets the backend and MQTT broker the gateway uses after saving.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(16.dp))

            val options = Environment.entries
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                options.forEachIndexed { index, environment ->
                    SegmentedButton(
                        selected = settings.environment == environment,
                        onClick = { onSelect(environment) },
                        shape = SegmentedButtonDefaults.itemShape(index, options.size),
                    ) { Text(environment.label) }
                }
            }
            Spacer(Modifier.height(16.dp))

            val editable = settings.environment == Environment.DEV
            OutlinedTextField(
                value = settings.backendUrl,
                onValueChange = { onUpdate(settings.copy(backendUrl = it)) },
                label = { Text("Backend URL") },
                enabled = editable,
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(
                value = settings.mqttHost,
                onValueChange = { onUpdate(settings.copy(mqttHost = it)) },
                label = { Text("MQTT host") },
                enabled = editable,
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(12.dp))
            Text(
                "MQTT ${settings.mqttPort} • " +
                    (if (settings.useTls) "TLS on" else "TLS off") +
                    if (editable) " • host editable in DEV" else " • PRODUCTION is fixed",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            if (state.envMismatch) {
                Spacer(Modifier.height(20.dp))
                EnvMismatchCard(state, onApply)
            }
        }
    }
}

// ---- Nạp firmware ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun FirmwareSheet(
    viewModel: SetupViewModel,
    state: UiState,
    onPickFile: () -> Unit,
    onDismiss: () -> Unit,
) {
    var confirming by remember { mutableStateOf(false) }
    val firmware = state.firmware

    ModalBottomSheet(onDismissRequest = { if (!firmware.uploading) onDismiss() }) {
        Column(Modifier.padding(horizontal = 24.dp).padding(bottom = 32.dp)) {
            Text("Flash firmware", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(4.dp))
            Text(
                "Push a .bin straight from the phone to the gateway over the SolarGW Wi-Fi. " +
                    "No internet, no backend, no cable.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(16.dp))

            state.gateway?.let { gateway ->
                Text(
                    "Running: ${gateway.fwVersion.ifEmpty { "unknown version" }}",
                    style = MaterialTheme.typography.bodyMedium,
                )
                Text(
                    "Device: ${gateway.deviceCode.ifEmpty { "not paired" }}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(12.dp))
            }

            OutlinedButton(
                onClick = onPickFile,
                enabled = !firmware.uploading,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (firmware.uri == null) "Choose a .bin file" else "Choose another file") }

            if (firmware.uri != null) {
                Spacer(Modifier.height(12.dp))
                Text(firmware.fileName, style = MaterialTheme.typography.bodyMedium)
                Text(
                    "${firmware.fileSize / 1024} KB",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            if (firmware.uploading) {
                Spacer(Modifier.height(16.dp))
                LinearProgressIndicator(
                    progress = { firmware.progress },
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(6.dp))
                Text(
                    "Flashing ${(firmware.progress * 100).toInt()}% — keep the screen on and stay " +
                        "on the SolarGW Wi-Fi.",
                    style = MaterialTheme.typography.bodySmall,
                )
            }

            firmware.error?.let {
                Spacer(Modifier.height(12.dp))
                Text(it, color = MaterialTheme.colorScheme.error)
            }

            if (firmware.done) {
                Spacer(Modifier.height(16.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        Icons.Rounded.CheckCircle,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                    )
                    Spacer(Modifier.size(8.dp))
                    Text("Flashed. The ESP32 is restarting.")
                }
            }

            Spacer(Modifier.height(16.dp))
            Button(
                onClick = { confirming = true },
                enabled = firmware.uri != null && !firmware.uploading,
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Flash to gateway") }
        }
    }

    if (confirming) {
        AlertDialog(
            onDismissRequest = { confirming = false },
            title = { Text("Flash this firmware?") },
            text = {
                Text(
                    "If this .bin cannot bring the SolarGW Wi-Fi back up, the recovery path " +
                        "through the phone is gone and you will need a USB cable. Only flash " +
                        "a file you know was built from this repo.",
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirming = false
                    viewModel.uploadFirmware()
                }) { Text("Flash") }
            },
            dismissButton = {
                TextButton(onClick = { confirming = false }) { Text("Cancel") }
            },
        )
    }
}

// ---- Bảo trì: đồng bộ lại / ghép IoT mới ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun MaintenanceSheet(
    viewModel: SetupViewModel,
    state: UiState,
    onDismiss: () -> Unit,
) {
    // null = không hỏi gì; true = xác nhận đồng bộ; false = xác nhận ghép mới.
    var confirmSync by remember { mutableStateOf<Boolean?>(null) }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 24.dp).padding(bottom = 32.dp)) {
            Text("Device maintenance", style = MaterialTheme.typography.titleLarge)
            state.gateway?.let {
                Spacer(Modifier.height(4.dp))
                Text(
                    "Paired with ${it.deviceCode}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.height(20.dp))

            Text("Resync battery configuration", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(
                "Use this after an admin adds or changes batteries in the site. The gateway " +
                    "restarts and reloads the battery list. Wi-Fi and IoT code are kept.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(8.dp))
            OutlinedButton(
                onClick = { confirmSync = true },
                enabled = !state.busy,
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Resync and restart") }

            Spacer(Modifier.height(24.dp))
            HorizontalDivider()
            Spacer(Modifier.height(24.dp))

            Text("Pair a new IoT", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(
                "Erases the IoT code, API key, site, battery mapping and MQTT settings so the " +
                    "gateway can pair with a different device. The current Wi-Fi is kept.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(8.dp))
            OutlinedButton(
                onClick = { confirmSync = false },
                enabled = !state.busy,
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = MaterialTheme.colorScheme.error,
                ),
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Erase pairing and scan a new QR") }
        }
    }

    confirmSync?.let { isSync ->
        AlertDialog(
            onDismissRequest = { confirmSync = null },
            title = { Text(if (isSync) "Resync now?" else "Erase IoT pairing?") },
            text = {
                Text(
                    if (isSync) {
                        "The gateway restarts and reloads the battery configuration from the backend."
                    } else {
                        "The current IoT code, API key, site and battery mapping are erased and " +
                            "CANNOT be recovered. After the restart you must scan the QR " +
                            "of the new IoT."
                    },
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmSync = null
                    onDismiss()
                    if (isSync) viewModel.reprovision() else viewModel.newPairing()
                }) { Text(if (isSync) "Resync" else "Erase") }
            },
            dismissButton = {
                TextButton(onClick = { confirmSync = null }) { Text("Cancel") }
            },
        )
    }
}
