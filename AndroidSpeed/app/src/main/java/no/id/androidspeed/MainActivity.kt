package no.id.androidspeed

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.InputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
const val MIN_PERIOD = 100f
const val MAX_PERIOD = 1000f
const val PAYLOAD_SIZE_TO_SEND = 4
const val RECV_SIZE = 2

class MainActivity : ComponentActivity() {

    private var btSocket: BluetoothSocket? = null
    private var outStream: OutputStream? = null
    private var inStream: InputStream? = null

    @SuppressLint("MissingPermission")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), 1)
        }

        val btManager = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
        val adapter = btManager.adapter

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    AppContent(adapter)
                }
            }
        }
    }

    @SuppressLint("MissingPermission")
    @Composable
    fun AppContent(adapter: BluetoothAdapter?) {
        val scope = rememberCoroutineScope()
        var isConnected by remember { mutableStateOf(false) }
        var errorMsg by remember { mutableStateOf<String?>(null) }
        var consoleLines by remember { mutableStateOf(listOf<String>()) }
        var period by remember { mutableFloatStateOf(MIN_PERIOD) }

        if (errorMsg != null) {
            AlertDialog(
                onDismissRequest = { errorMsg = null },
                confirmButton = { TextButton(onClick = { errorMsg = null }) { Text("OK") } },
                title = { Text("System Alert") },
                text = { Text(errorMsg!!) }
            )
        }

        if (!isConnected) {
            val pairedDevices = adapter?.bondedDevices?.toList() ?: emptyList()
            LazyColumn(modifier = Modifier.fillMaxSize().padding(16.dp)) {
                item {
                    Text("Bluetooth Devices", style = MaterialTheme.typography.headlineSmall)
                    Spacer(modifier = Modifier.height(16.dp))
                }
                items(pairedDevices) { device ->
                    Button(
                        onClick = {
                            scope.launch {
                                try {
                                    btSocket = device.createRfcommSocketToServiceRecord(SPP_UUID)
                                    withContext(Dispatchers.IO) { btSocket!!.connect() }
                                    outStream = btSocket!!.outputStream
                                    inStream = btSocket!!.inputStream
                                    isConnected = true
                                    startListening { line ->
                                        consoleLines = (consoleLines + line).takeLast(50)
                                    }
                                } catch (e: Exception) {
                                    errorMsg = "Connection Failed: ${e.message}"
                                }
                            }
                        },
                        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)
                    ) {
                        Text(device.name ?: "Unknown Device")
                    }
                }
            }
        } else {
            Column(modifier = Modifier.fillMaxSize().padding(16.dp)) {
                Text("Período: ${period.toInt()}")
                Slider(
                    value = period,
                    onValueChange = { period = it },
                    valueRange = MIN_PERIOD..MAX_PERIOD
                )

                Button(
                    onClick = { sendData(period.toInt(), PAYLOAD_SIZE_TO_SEND) },
                    modifier = Modifier.fillMaxWidth().padding(vertical = 16.dp)
                ) {
                    Text("Send Package")
                }

                Text("Receive Console", style = MaterialTheme.typography.titleMedium)
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .background(Color(0xFF1E1E1E))
                        .padding(8.dp)
                ) {
                    LazyColumn(reverseLayout = true) {
                        items(consoleLines.reversed()) { line ->
                            Text(line, color = Color(0xFF00FF00), fontFamily = FontFamily.Monospace)
                        }
                    }
                }
            }
        }
    }

    private fun startListening(onData: (String) -> Unit) {
        Thread {
            var state = 0
            val buffer = ByteArray(RECV_SIZE)
            var index = 0

            try {
                while (true) {
                    val b = inStream?.read() ?: break
                    if (b == -1) break

                    when (state) {
                        0 -> if (b == 0x3C) { // '<'
                            state = 1
                            index = 0
                        }
                        1 -> {
                            buffer[index++] = b.toByte()
                            if (index == RECV_SIZE) state = 2
                        }
                        2 -> {
                            if (b == 0x3E) { // '>'
                                val value = if (RECV_SIZE == 4) {
                                    ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN).int
                                } else {
                                    ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN).short.toInt()
                                }
                                runOnUiThread { onData("OUT: $value") }
                            }
                            state = 0
                        }
                    }
                }
            } catch (e: Exception) {
                runOnUiThread { onData("ERROR: ${e.message}") }
            }
        }.start()
    }

    private fun sendData(value: Int, size: Int) {
        try {
            val buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
            if (size == 4) buffer.putInt(value) else buffer.putShort(value.toShort())

            val payload = buffer.array()
            val packet = ByteArray(size + 2)
            packet[0] = 0x3C // '<'
            System.arraycopy(payload, 0, packet, 1, size)
            packet[size + 1] = 0x3E // '>'

            outStream?.write(packet)
            outStream?.flush()
        } catch (e: Exception) {
            // Socket write error handled silently or via state
        }
    }
}