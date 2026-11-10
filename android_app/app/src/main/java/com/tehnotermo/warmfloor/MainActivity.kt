package com.tehnotermo.warmfloor

import android.app.Activity
import android.content.Intent
import android.content.res.ColorStateList
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.tehnotermo.warmfloor.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var mqttHelper: MqttHelper? = null

    // Локальное текущее состояние
    private var currentFloorSet = 25.0f
    private var currentAirSet = 22.0f
    private var currentHeatingEnabled = true
    private var isDeviceOnline = false

    private val settingsLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            reconnectMqtt()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupListeners()
    }

    override fun onStart() {
        super.onStart()
        reconnectMqtt()
    }

    override fun onDestroy() {
        super.onDestroy()
        mqttHelper?.disconnect()
    }

    private fun setupListeners() {
        binding.btnSettings.setOnClickListener {
            val intent = Intent(this, SettingsActivity::class.java)
            settingsLauncher.launch(intent)
        }

        // Главный выключатель
        binding.btnMasterPower.setOnClickListener {
            val newState = !currentHeatingEnabled
            mqttHelper?.publishMasterPower(newState)
            // Оптимистичное локальное обновление
            currentHeatingEnabled = newState
            updateMasterPowerButton(newState)
        }

        // Регулировка уставки пола (+ / - 0.5°C)
        binding.btnFloorMinus.setOnClickListener {
            currentFloorSet = (currentFloorSet - 0.5f).coerceAtLeast(10.0f)
            binding.tvFloorTarget.text = String.format(java.util.Locale.US, "%.1f °C", currentFloorSet)
            mqttHelper?.publishFloorTarget(currentFloorSet)
        }

        binding.btnFloorPlus.setOnClickListener {
            currentFloorSet = (currentFloorSet + 0.5f).coerceAtMost(36.0f)
            binding.tvFloorTarget.text = String.format(java.util.Locale.US, "%.1f °C", currentFloorSet)
            mqttHelper?.publishFloorTarget(currentFloorSet)
        }

        // Регулировка уставки воздуха (+ / - 0.5°C)
        binding.btnAirMinus.setOnClickListener {
            currentAirSet = (currentAirSet - 0.5f).coerceAtLeast(10.0f)
            binding.tvAirTarget.text = String.format(java.util.Locale.US, "%.1f °C", currentAirSet)
            mqttHelper?.publishAirTarget(currentAirSet)
        }

        binding.btnAirPlus.setOnClickListener {
            currentAirSet = (currentAirSet + 0.5f).coerceAtMost(35.0f)
            binding.tvAirTarget.text = String.format(java.util.Locale.US, "%.1f °C", currentAirSet)
            mqttHelper?.publishAirTarget(currentAirSet)
        }
    }

    private fun reconnectMqtt() {
        val config = SettingsActivity.loadConfig(this)
        mqttHelper?.disconnect()

        binding.tvMqttStatus.text = "Подключение к ${config.host}..."
        binding.viewStatusDot.backgroundTintList = ColorStateList.valueOf(
            ContextCompat.getColor(this, R.color.color_warning)
        )

        mqttHelper = MqttHelper(config).apply {
            onConnectionChanged = { connected, message ->
                if (connected) {
                    binding.tvMqttStatus.text = if (isDeviceOnline) "В сети (Плата Онлайн)" else "Брокер подключен"
                    binding.viewStatusDot.backgroundTintList = ColorStateList.valueOf(
                        ContextCompat.getColor(this@MainActivity, R.color.color_success)
                    )
                } else {
                    binding.tvMqttStatus.text = "Ошибка: $message"
                    binding.viewStatusDot.backgroundTintList = ColorStateList.valueOf(
                        ContextCompat.getColor(this@MainActivity, R.color.color_heat_on)
                    )
                }
            }

            onDeviceStatusReceived = { status ->
                isDeviceOnline = (status.equals("online", ignoreCase = true))
                if (isDeviceOnline) {
                    binding.tvMqttStatus.text = "В сети (Плата Онлайн)"
                    binding.viewStatusDot.backgroundTintList = ColorStateList.valueOf(
                        ContextCompat.getColor(this@MainActivity, R.color.color_success)
                    )
                } else {
                    binding.tvMqttStatus.text = "Плата не в сети (offline)"
                    binding.viewStatusDot.backgroundTintList = ColorStateList.valueOf(
                        ContextCompat.getColor(this@MainActivity, R.color.color_warning)
                    )
                }
            }

            onStateReceived = { state ->
                renderState(state)
            }
        }

        mqttHelper?.connect()
    }

    private fun renderState(state: FloorState) {
        // Уставки
        currentFloorSet = state.floorSet
        binding.tvFloorTarget.text = String.format(java.util.Locale.US, "%.1f °C", currentFloorSet)

        currentAirSet = state.airSet
        binding.tvAirTarget.text = String.format(java.util.Locale.US, "%.1f °C", currentAirSet)

        // Главное питание
        currentHeatingEnabled = (state.heatingEnabled == 1)
        updateMasterPowerButton(currentHeatingEnabled)

        // Зона 1
        if (state.zone1Temp > -100.0f) {
            binding.tvZ1Temp.text = String.format(java.util.Locale.US, "%.1f", state.zone1Temp)
        } else {
            binding.tvZ1Temp.text = "--.-"
        }
        updateZoneBadge(binding.badgeZ1Relay, state.zone1Heat == 1, state.airDemand == 1, currentHeatingEnabled)

        // Зона 2
        if (state.zone2Temp > -100.0f) {
            binding.tvZ2Temp.text = String.format(java.util.Locale.US, "%.1f", state.zone2Temp)
        } else {
            binding.tvZ2Temp.text = "--.-"
        }
        updateZoneBadge(binding.badgeZ2Relay, state.zone2Heat == 1, state.airDemand == 1, currentHeatingEnabled)

        // Воздух
        if (state.airTemp > -100.0f) {
            binding.tvAirTemp.text = String.format(java.util.Locale.US, "%.1f", state.airTemp)
        } else {
            binding.tvAirTemp.text = "--.-"
        }

        if (!currentHeatingEnabled) {
            binding.badgeAirDemand.text = "Отопление выкл"
            binding.badgeAirDemand.setTextColor(ContextCompat.getColor(this, R.color.text_muted))
        } else if (state.airDemand == 1) {
            binding.badgeAirDemand.text = "Требует тепла"
            binding.badgeAirDemand.setTextColor(ContextCompat.getColor(this, R.color.accent_blue))
        } else {
            binding.badgeAirDemand.text = "Воздух прогрет"
            binding.badgeAirDemand.setTextColor(ContextCompat.getColor(this, R.color.color_success))
        }
    }

    private fun updateZoneBadge(badge: android.widget.TextView, relayOn: Boolean, airDemand: Boolean, heatingEn: Boolean) {
        if (!heatingEn) {
            badge.text = "ВЫКЛ"
            badge.setTextColor(ContextCompat.getColor(this, R.color.text_muted))
        } else if (relayOn) {
            badge.text = "НАГРЕВ (SSR ВКЛ)"
            badge.setTextColor(ContextCompat.getColor(this, R.color.color_heat_on))
        } else if (!airDemand) {
            badge.text = "ОСТАНОВ ВОЗДУХОМ"
            badge.setTextColor(ContextCompat.getColor(this, R.color.color_warning))
        } else {
            badge.text = "ПОЛ ПРОГРЕТ"
            badge.setTextColor(ContextCompat.getColor(this, R.color.color_success))
        }
    }

    private fun updateMasterPowerButton(enabled: Boolean) {
        if (enabled) {
            binding.btnMasterPower.text = "ОТОПЛЕНИЕ ВКЛЮЧЕНО"
            binding.btnMasterPower.backgroundTintList = ColorStateList.valueOf(
                ContextCompat.getColor(this, R.color.color_heat_on)
            )
        } else {
            binding.btnMasterPower.text = "ОТОПЛЕНИЕ ВЫКЛЮЧЕНО"
            binding.btnMasterPower.backgroundTintList = ColorStateList.valueOf(
                ContextCompat.getColor(this, R.color.color_heat_off)
            )
        }
    }
}
