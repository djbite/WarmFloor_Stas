package com.tehnotermo.warmfloor

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.google.gson.Gson
import org.eclipse.paho.client.mqttv3.*
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import java.nio.charset.StandardCharsets

class MqttHelper(private val config: MqttConfig) {

    private val tag = "MqttHelper"
    private var client: MqttAsyncClient? = null
    private val mainHandler = Handler(Looper.getMainLooper())
    private val gson = Gson()

    var onConnectionChanged: ((Boolean, String) -> Unit)? = null
    var onStateReceived: ((FloorState) -> Unit)? = null
    var onDeviceStatusReceived: ((String) -> Unit)? = null

    val isConnected: Boolean
        get() = client?.isConnected == true

    fun connect() {
        disconnect()

        val serverUri = "tcp://${config.host.trim()}:${config.port}"
        val clientId = "WarmFloorApp_" + System.currentTimeMillis().toString().takeLast(6)

        try {
            client = MqttAsyncClient(serverUri, clientId, MemoryPersistence())
            val options = MqttConnectOptions().apply {
                isCleanSession = true
                connectionTimeout = 10
                keepAliveInterval = 20
                isAutomaticReconnect = true
                if (config.user.isNotBlank()) {
                    userName = config.user.trim()
                }
                if (config.pass.isNotBlank()) {
                    password = config.pass.trim().toCharArray()
                }
            }

            client?.setCallback(object : MqttCallbackExtended {
                override fun connectComplete(reconnect: Boolean, serverURI: String?) {
                    Log.d(tag, "MQTT connectComplete (reconnect=$reconnect)")
                    mainHandler.post {
                        onConnectionChanged?.invoke(true, "Подключено к брокеру")
                    }
                    subscribeTopics()
                }

                override fun connectionLost(cause: Throwable?) {
                    val errMsg = cause?.message ?: "Потеряна связь"
                    Log.w(tag, "MQTT connectionLost: $errMsg")
                    mainHandler.post {
                        onConnectionChanged?.invoke(false, errMsg)
                    }
                }

                override fun messageArrived(topic: String?, message: MqttMessage?) {
                    if (topic == null || message == null) return
                    val payload = String(message.payload, StandardCharsets.UTF_8)
                    Log.d(tag, "Пришло сообщение [$topic]: $payload")

                    val prefix = config.prefix.trim().trimEnd('/')

                    if (topic == "$prefix/state") {
                        try {
                            val state = gson.fromJson(payload, FloorState::class.java)
                            mainHandler.post {
                                onStateReceived?.invoke(state)
                            }
                        } catch (e: Exception) {
                            Log.e(tag, "Ошибка парсинга state JSON: ${e.message}")
                        }
                    } else if (topic == "$prefix/status") {
                        mainHandler.post {
                            onDeviceStatusReceived?.invoke(payload.trim())
                        }
                    }
                }

                override fun deliveryComplete(token: IMqttDeliveryToken?) {
                }
            })

            client?.connect(options, null, object : IMqttActionListener {
                override fun onSuccess(asyncActionToken: IMqttToken?) {
                    Log.i(tag, "Успешное подключение к $serverUri")
                }

                override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                    val error = exception?.localizedMessage ?: "Ошибка подключения"
                    Log.e(tag, "Ошибка подключения к $serverUri: $error")
                    mainHandler.post {
                        onConnectionChanged?.invoke(false, error)
                    }
                }
            })

        } catch (e: Exception) {
            Log.e(tag, "Исключение при инициализации MQTT: ${e.message}")
            onConnectionChanged?.invoke(false, e.localizedMessage ?: "Ошибка")
        }
    }

    private fun subscribeTopics() {
        val prefix = config.prefix.trim().trimEnd('/')
        val topics = arrayOf("$prefix/state", "$prefix/status")
        val qos = intArrayOf(1, 1)

        client?.subscribe(topics, qos, null, object : IMqttActionListener {
            override fun onSuccess(asyncActionToken: IMqttToken?) {
                Log.d(tag, "Подписка на топики успешна: ${topics.joinToString()}")
            }

            override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                Log.e(tag, "Ошибка подписки на топики: ${exception?.message}")
            }
        })
    }

    fun publishFloorTarget(targetTemp: Float) {
        val prefix = config.prefix.trim().trimEnd('/')
        val topic = "$prefix/floor/set_target"
        val payload = String.format(java.util.Locale.US, "%.1f", targetTemp)
        publish(topic, payload)
    }

    fun publishAirTarget(targetTemp: Float) {
        val prefix = config.prefix.trim().trimEnd('/')
        val topic = "$prefix/air/set_target"
        val payload = String.format(java.util.Locale.US, "%.1f", targetTemp)
        publish(topic, payload)
    }

    fun publishMasterPower(enabled: Boolean) {
        val prefix = config.prefix.trim().trimEnd('/')
        val topic = "$prefix/power"
        val payload = if (enabled) "1" else "0"
        publish(topic, payload)
    }

    private fun publish(topic: String, payload: String, retained: Boolean = false) {
        if (client?.isConnected != true) {
            Log.w(tag, "Попытка публикации без подключения к брокеру: $topic")
            return
        }
        try {
            val message = MqttMessage(payload.toByteArray(StandardCharsets.UTF_8)).apply {
                this.qos = 1
                this.isRetained = retained
            }
            client?.publish(topic, message, null, object : IMqttActionListener {
                override fun onSuccess(asyncActionToken: IMqttToken?) {
                    Log.d(tag, "Успешно отправлено [$topic]: $payload")
                }

                override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                    Log.e(tag, "Ошибка отправки [$topic]: ${exception?.message}")
                }
            })
        } catch (e: Exception) {
            Log.e(tag, "Исключение при публикации: ${e.message}")
        }
    }

    fun disconnect() {
        try {
            if (client?.isConnected == true) {
                client?.disconnect()
            }
            client?.close()
        } catch (e: Exception) {
            // ignore
        }
        client = null
    }
}
