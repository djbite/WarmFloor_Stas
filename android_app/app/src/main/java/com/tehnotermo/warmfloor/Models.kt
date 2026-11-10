package com.tehnotermo.warmfloor

import com.google.gson.annotations.SerializedName

/**
 * Модель данных телеметрии, передаваемой контроллером ESP32 в топик warmfloor/state
 */
data class FloorState(
    @SerializedName("fl_set")
    val floorSet: Float = 25.0f,

    @SerializedName("ar_set")
    val airSet: Float = 22.0f,

    @SerializedName("z1_t")
    val zone1Temp: Float = -127.0f,

    @SerializedName("z1_h")
    val zone1Heat: Int = 0, // 1 = SSR замкнуто, 0 = выключено

    @SerializedName("z2_t")
    val zone2Temp: Float = -127.0f,

    @SerializedName("z2_h")
    val zone2Heat: Int = 0, // 1 = SSR замкнуто, 0 = выключено

    @SerializedName("air")
    val airTemp: Float = -127.0f,

    @SerializedName("ar_dem")
    val airDemand: Int = 1, // 1 = требует тепла, 0 = воздух прогрет

    @SerializedName("heat")
    val heatingEnabled: Int = 1 // 1 = отопление ВКЛ, 0 = отопление ВЫКЛ
)

/**
 * Настройки подключения к MQTT брокеру (WQTT)
 */
data class MqttConfig(
    val host: String = "wqtt.ru",
    val port: Int = 1883,
    val user: String = "",
    val pass: String = "",
    val prefix: String = "warmfloor"
)
