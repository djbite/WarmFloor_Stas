package com.tehnotermo.warmfloor

import android.content.Context
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.tehnotermo.warmfloor.databinding.ActivitySettingsBinding

class SettingsActivity : AppCompatActivity() {

    private lateinit var binding: ActivitySettingsBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        binding.etBrokerHost.setText(prefs.getString(KEY_HOST, "wqtt.ru"))
        binding.etBrokerPort.setText(prefs.getInt(KEY_PORT, 1883).toString())
        binding.etMqttUser.setText(prefs.getString(KEY_USER, ""))
        binding.etMqttPassword.setText(prefs.getString(KEY_PASS, ""))
        binding.etTopicPrefix.setText(prefs.getString(KEY_PREFIX, "warmfloor"))

        binding.btnSaveSettings.setOnClickListener {
            val host = binding.etBrokerHost.text.toString().trim()
            val portStr = binding.etBrokerPort.text.toString().trim()
            val user = binding.etMqttUser.text.toString().trim()
            val pass = binding.etMqttPassword.text.toString().trim()
            val prefix = binding.etTopicPrefix.text.toString().trim()

            if (host.isEmpty()) {
                binding.etBrokerHost.error = "Введите адрес брокера"
                return@setOnClickListener
            }

            val port = portStr.toIntOrNull() ?: 1883

            prefs.edit()
                .putString(KEY_HOST, host)
                .putInt(KEY_PORT, port)
                .putString(KEY_USER, user)
                .putString(KEY_PASS, pass)
                .putString(KEY_PREFIX, if (prefix.isEmpty()) "warmfloor" else prefix)
                .apply()

            Toast.makeText(this, "Настройки сохранены!", Toast.LENGTH_SHORT).show()
            setResult(RESULT_OK)
            finish()
        }
    }

    companion object {
        const val PREFS_NAME = "warmfloor_prefs"
        const val KEY_HOST = "mqtt_host"
        const val KEY_PORT = "mqtt_port"
        const val KEY_USER = "mqtt_user"
        const val KEY_PASS = "mqtt_pass"
        const val KEY_PREFIX = "mqtt_prefix"

        fun loadConfig(context: Context): MqttConfig {
            val p = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            return MqttConfig(
                host = p.getString(KEY_HOST, "wqtt.ru") ?: "wqtt.ru",
                port = p.getInt(KEY_PORT, 1883),
                user = p.getString(KEY_USER, "") ?: "",
                pass = p.getString(KEY_PASS, "") ?: "",
                prefix = p.getString(KEY_PREFIX, "warmfloor") ?: "warmfloor"
            )
        }
    }
}
