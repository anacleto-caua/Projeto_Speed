package no.id.androidspeed

import android.Manifest
import android.annotation.SuppressLint
import android.app.Application
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.io.InputStream
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.*

// CONFIGURATION
private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

// STATE
enum class ConnectionState { DISCONNECTED, CONNECTING, CONNECTED }

data class AppState(
    val connectionState: ConnectionState = ConnectionState.DISCONNECTED,
    val selectedDevice: BluetoothDevice? = null,
    val connectedDeviceName: String? = null,
    val participantName: String = "",
    val intervalValue: Float = 0f, // Range: 0 to 5000
    val activeLogFileUri: Uri? = null,
    val activeLogFileName: String? = null,
    val consoleLogs: List<String> = emptyList()
)

// VIEW MODEL
@SuppressLint("MissingPermission")
class MainViewModel(application: Application) : AndroidViewModel(application) {

    private val _state = MutableStateFlow(AppState())
    val state: StateFlow<AppState> = _state.asStateFlow()

    private var btSocket: android.bluetooth.BluetoothSocket? = null
    private var outStream: OutputStream? = null
    private var inStream: InputStream? = null
    private var listeningJob: Job? = null

    private val contentResolver = application.contentResolver

    private var activeFileOutputStream: OutputStream? = null

    // Clean exit
    override fun onCleared() {
        super.onCleared()
        disconnect()
        try {
            activeFileOutputStream?.close()
        } catch (e: Exception) { /* Ignore on shutdown */ }
    }

    // State updates
    fun selectDevice(device: BluetoothDevice) = _state.update { it.copy(selectedDevice = device) }
    fun updateParticipantName(name: String) = _state.update { it.copy(participantName = name) }
    fun updateIntervalValue(value: Float) = _state.update { it.copy(intervalValue = value) }
    fun logToConsole(message: String) {
        _state.update {
            val newLogs = it.consoleLogs.toMutableList()
            newLogs.add(0, "[${getTimestamp()}] $message")
            it.copy(consoleLogs = newLogs.take(100))
        }
    }

    // File managment
    fun setLogFile(uri: Uri, name: String?, isNewFile: Boolean) {
        _state.update { it.copy(activeLogFileUri = uri, activeLogFileName = name) }
        logToConsole("Target file set: $name")

        try {
            // Take persistable URI permissions to survive backgrounding
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_WRITE_URI_PERMISSION)

            // Close the old stream if one exists, open the new one
            activeFileOutputStream?.close()
            activeFileOutputStream = contentResolver.openOutputStream(uri, "wa")

            if (isNewFile) {
                viewModelScope.launch(Dispatchers.IO) {
                    writeToCsv("Timestamp,Name,Status,Reaction Time\n")
                }
            }
        } catch (e: Exception) {
            logToConsole("FILE ERROR: Failed to open or secure file stream.")
        }
    }

    private suspend fun writeToCsv(data: String) = withContext(Dispatchers.IO) {
        try {
            activeFileOutputStream?.write(data.toByteArray())
            activeFileOutputStream?.flush() // Push data immediately, do not close stream
        } catch (e: Exception) {
            withContext(Dispatchers.Main) {
                logToConsole("FILE ERROR: Unable to write data.")
            }
        }
    }

    // Bluetooth
    fun toggleConnection() {
        val currentState = _state.value
        if (currentState.connectionState == ConnectionState.CONNECTED || currentState.connectionState == ConnectionState.CONNECTING) {
            disconnect()
        } else if (currentState.selectedDevice != null) {
            connectToDevice(currentState.selectedDevice)
        }
    }

    private fun connectToDevice(device: BluetoothDevice) {
        _state.update { it.copy(connectionState = ConnectionState.CONNECTING) }
        logToConsole("Attempting to connect to ${device.name} (Timeout: 5s)...")

        viewModelScope.launch(Dispatchers.IO) {
            try {
                btSocket = device.createRfcommSocketToServiceRecord(SPP_UUID)

                // Parallel kill switch to unblock the native OS socket leak
                val timeoutJob = launch {
                    delay(5000L)
                    btSocket?.close()
                }

                btSocket?.connect()
                timeoutJob.cancel() // Cancel kill switch if connection succeeds

                outStream = btSocket?.outputStream
                inStream = btSocket?.inputStream

                withContext(Dispatchers.Main) {
                    _state.update {
                        it.copy(
                            connectionState = ConnectionState.CONNECTED,
                            connectedDeviceName = device.name
                        )
                    }
                    logToConsole("Connected successfully.")
                    startListening()
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    disconnect()
                    logToConsole("Connection failed or timed out.")
                }
            }
        }
    }

    fun disconnect() {
        listeningJob?.cancel()
        try {
            btSocket?.close()
        } catch (e: Exception) { /* Ignore */ }
        btSocket = null
        outStream = null
        inStream = null
        _state.update {
            it.copy(
                connectionState = ConnectionState.DISCONNECTED,
                connectedDeviceName = null
            )
        }
        logToConsole("Disconnected.")
    }

    fun sendIntervalCommand() {
        if (_state.value.connectionState != ConnectionState.CONNECTED) return
        val commandValue = _state.value.intervalValue.toInt()
        val payload = "<$commandValue>"

        viewModelScope.launch(Dispatchers.IO) {
            try {
                outStream?.write(payload.toByteArray())
                outStream?.flush()
                withContext(Dispatchers.Main) {
                    logToConsole("TX: $payload")
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    logToConsole("TX Error: ${e.message}")
                    disconnect()
                }
            }
        }
    }

    private fun startListening() {
        listeningJob = viewModelScope.launch(Dispatchers.IO) {
            val buffer = ByteArray(1024)
            val stringBuilder = StringBuilder()

            try {
                while (true) {
                    val bytesRead = inStream?.read(buffer) ?: break
                    if (bytesRead == -1) break

                    stringBuilder.append(String(buffer, 0, bytesRead))

                    // Memory leak protection
                    if (stringBuilder.length > 4096) {
                        stringBuilder.clear()
                        withContext(Dispatchers.Main) {
                            logToConsole("RX Warning: Buffer overflow cleared due to corrupted stream.")
                        }
                        continue
                    }

                    while (true) {
                        val startIdx = stringBuilder.indexOf("<")
                        if (startIdx == -1) break

                        if (startIdx > 0) {
                            stringBuilder.delete(0, startIdx)
                        }

                        val endIdx = stringBuilder.indexOf(">")
                        if (endIdx == -1) break

                        val payload = stringBuilder.substring(1, endIdx)
                        processIncomingPayload(payload)

                        stringBuilder.delete(0, endIdx + 1)
                    }
                    delay(10)
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    logToConsole("RX Error: Device disconnected.")
                    disconnect()
                }
            }
        }
    }

    private suspend fun processIncomingPayload(payload: String) {
        val currentState = _state.value
        val isNumeric = payload.matches("-?\\d+(\\.\\d+)?".toRegex())
        val status = if (isNumeric) "OK" else "BAD_DATA"

        val logEntry = "RX: <$payload> [$status]"

        withContext(Dispatchers.Main) {
            logToConsole(logEntry)
        }

        if (currentState.activeLogFileUri == null) {
            withContext(Dispatchers.Main) {
                logToConsole("NOT SAVED: No file active.")
            }
            return
        }

        val timestamp = getTimestamp(includeMillis = true)
        val csvRow = "$timestamp,${currentState.participantName},$status,$payload\n"
        writeToCsv(csvRow)
    }

    private fun getTimestamp(includeMillis: Boolean = false): String {
        val pattern = if (includeMillis) "yyyy-MM-dd HH:mm:ss.SSS" else "HH:mm:ss"
        return SimpleDateFormat(pattern, Locale.getDefault()).format(Date())
    }
}

// MAIN ACTIVITY & UI LAYER STUFF
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val adapter = bluetoothManager.adapter

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    val viewModel: MainViewModel = viewModel(
                        factory = ViewModelProvider.AndroidViewModelFactory.getInstance(application)
                    )
                    AppUI(viewModel, adapter)
                }
            }
        }
    }
}

@Composable
fun AppUI(viewModel: MainViewModel, btAdapter: BluetoothAdapter?) {
    val state by viewModel.state.collectAsState()
    val context = LocalContext.current

    var hasBluetoothPermissions by remember { mutableStateOf(false) }

    val permissionsToRequest = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN)
    } else {
        arrayOf(Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN)
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { permissionsMap ->
        hasBluetoothPermissions = permissionsMap.values.all { it }
    }

    LaunchedEffect(Unit) {
        val permissionsGranted = permissionsToRequest.all {
            ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
        }
        if (permissionsGranted) {
            hasBluetoothPermissions = true
        } else {
            permissionLauncher.launch(permissionsToRequest)
        }
    }

    val createDocumentLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("text/csv")
    ) { uri: Uri? ->
        uri?.let {
            viewModel.setLogFile(it, "Active Log File", isNewFile = true)
        }
    }

    val openDocumentLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let {
            viewModel.setLogFile(it, "Resumed Log File", isNewFile = false)
        }
    }

    Column(modifier = Modifier.fillMaxSize().padding(16.dp)) {
        if (!hasBluetoothPermissions) {
            Text("Bluetooth permissions required to connect to hardware.", color = Color.Red)
            Spacer(modifier = Modifier.height(16.dp))
        }

        ConnectionZone(state, btAdapter, viewModel, hasBluetoothPermissions)
        Divider(modifier = Modifier.padding(vertical = 8.dp))

        FileZone(
            state = state,
            onCreateFileClick = {
                val defaultName = "Subject_${state.participantName.ifEmpty { "Unknown" }}_${
                    SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                }.csv"
                createDocumentLauncher.launch(defaultName)
            },
            onOpenFileClick = {
                openDocumentLauncher.launch(arrayOf("text/comma-separated-values", "text/csv"))
            }
        )
        Divider(modifier = Modifier.padding(vertical = 8.dp))

        SessionZone(state, viewModel)
        Divider(modifier = Modifier.padding(vertical = 8.dp))

        ControlZone(state, viewModel)
        Divider(modifier = Modifier.padding(vertical = 8.dp))

        ConsoleZone(state)
    }
}

@SuppressLint("MissingPermission")
@Composable
fun ConnectionZone(
    state: AppState,
    btAdapter: BluetoothAdapter?,
    viewModel: MainViewModel,
    hasPermissions: Boolean
) {
    var expanded by remember { mutableStateOf(false) }

    val pairedDevices = remember(hasPermissions) {
        if (hasPermissions) btAdapter?.bondedDevices?.toList() ?: emptyList() else emptyList()
    }

    Column {
        Text("Device Connection", style = MaterialTheme.typography.titleMedium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.weight(1f)) {
                Button(
                    onClick = { expanded = true },
                    enabled = state.connectionState == ConnectionState.DISCONNECTED && hasPermissions,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(state.selectedDevice?.name ?: "Select Device")
                }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    pairedDevices.forEach { device ->
                        DropdownMenuItem(
                            text = { Text(device.name) },
                            onClick = {
                                viewModel.selectDevice(device)
                                expanded = false
                            }
                        )
                    }
                }
            }
            Spacer(modifier = Modifier.width(8.dp))
            Button(
                onClick = { viewModel.toggleConnection() },
                enabled = state.selectedDevice != null && hasPermissions,
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (state.connectionState == ConnectionState.CONNECTED) Color.Red else MaterialTheme.colorScheme.primary
                )
            ) {
                Text(
                    when (state.connectionState) {
                        ConnectionState.DISCONNECTED -> "Connect"
                        ConnectionState.CONNECTING -> "Connecting..."
                        ConnectionState.CONNECTED -> "Disconnect"
                    }
                )
            }
        }
    }
}

@Composable
fun FileZone(state: AppState, onCreateFileClick: () -> Unit, onOpenFileClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.weight(1f)) {
            Text("Session Log File", style = MaterialTheme.typography.titleMedium)
            Text(
                text = state.activeLogFileName ?: "No active file selected",
                style = MaterialTheme.typography.bodySmall,
                color = if (state.activeLogFileUri == null) Color.Red else Color.Green
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onOpenFileClick) {
                Text("Open")
            }
            Button(onClick = onCreateFileClick) {
                Text("New")
            }
        }
    }
}

@Composable
fun SessionZone(state: AppState, viewModel: MainViewModel) {
    Column {
        Text("Participant Info", style = MaterialTheme.typography.titleMedium)
        OutlinedTextField(
            value = state.participantName,
            onValueChange = { viewModel.updateParticipantName(it) },
            label = { Text("Subject Name / ID") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true
        )
    }
}

@Composable
fun ControlZone(state: AppState, viewModel: MainViewModel) {
    Column {
        Row(verticalAlignment = Alignment.Bottom) {
            Text("Interval Control", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
            Text("${state.intervalValue.toInt()} ms", style = MaterialTheme.typography.titleLarge)
        }
        Slider(
            value = state.intervalValue,
            onValueChange = { viewModel.updateIntervalValue(it) },
            valueRange = 0f..5000f,
            enabled = state.connectionState == ConnectionState.CONNECTED
        )
        Button(
            onClick = { viewModel.sendIntervalCommand() },
            enabled = state.connectionState == ConnectionState.CONNECTED,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Send Command")
        }
    }
}

@Composable
fun ConsoleZone(state: AppState) {
    Column(modifier = Modifier.fillMaxSize()) {
        Text("Live Console", style = MaterialTheme.typography.titleMedium)
        Spacer(modifier = Modifier.height(4.dp))
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(0xFF1E1E1E))
                .padding(8.dp)
        ) {
            LazyColumn(reverseLayout = true) {
                items(state.consoleLogs) { log ->
                    val color = when {
                        log.contains("!!!") -> Color(0xFFFF5555)
                        log.contains("TX:") -> Color(0xFF55FFFF)
                        log.contains("RX:") -> Color(0xFF55FF55)
                        else -> Color.LightGray
                    }
                    Text(log, color = color, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}
