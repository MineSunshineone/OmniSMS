package com.omnisms.app

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {
    private lateinit var configEditor: EditText
    private lateinit var smsTarget: EditText
    private lateinit var smsContent: EditText
    private lateinit var ussdCode: EditText
    private lateinit var sendUssdButton: Button
    private lateinit var statusText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        bindViews()
        bindActions()
        if (savedInstanceState == null) configEditor.setText(ConfigStore.load(this))
        AndroidScheduler.ensureScheduled(applicationContext)
        requestRuntimePermissions()
    }

    private fun bindViews() {
        configEditor = findViewById(R.id.configEditor)
        smsTarget = findViewById(R.id.smsTargetInput)
        smsContent = findViewById(R.id.smsContentInput)
        ussdCode = findViewById(R.id.ussdInput)
        sendUssdButton = findViewById(R.id.sendUssdButton)
        statusText = findViewById(R.id.statusText)
    }

    private fun bindActions() {
        findViewById<Button>(R.id.startServiceButton).setOnClickListener { startOmniSmsService() }
        findViewById<Button>(R.id.sendSmsButton).setOnClickListener { sendSms() }
        sendUssdButton.setOnClickListener { sendUssd() }
        findViewById<Button>(R.id.saveConfigButton).setOnClickListener { saveConfig() }
    }

    private fun startOmniSmsService() {
        val result = runCatching {
            val intent = Intent(this, OmniSmsService::class.java)
            startForegroundService(intent)
        }
        result.fold(
            onSuccess = { showStatus(getString(R.string.service_started), StatusKind.SUCCESS) },
            onFailure = {
                showStatus(
                    getString(R.string.service_failed, it.message ?: getString(R.string.unknown_error)),
                    StatusKind.ERROR,
                )
            },
        )
    }

    private fun sendSms() {
        val target = smsTarget.text.toString().trim()
        val content = smsContent.text.toString()
        if (target.isEmpty()) {
            smsTarget.requestFocus()
            showStatus(getString(R.string.sms_missing_target), StatusKind.ERROR)
            return
        }
        if (content.isBlank()) {
            smsContent.requestFocus()
            showStatus(getString(R.string.sms_missing_content), StatusKind.ERROR)
            return
        }
        val result = TelephonyGateway.sendSms(this, target, content)
        InboxStore.addSent(this, target, content, result.isSuccess)
        result.fold(
            onSuccess = {
                smsContent.text.clear()
                showStatus(getString(R.string.sms_submitted), StatusKind.SUCCESS)
            },
            onFailure = {
                showStatus(
                    getString(R.string.sms_failed, it.message ?: getString(R.string.unknown_error)),
                    StatusKind.ERROR,
                )
            },
        )
    }

    private fun sendUssd() {
        val code = ussdCode.text.toString().trim()
        if (code.isEmpty()) {
            ussdCode.requestFocus()
            showStatus(getString(R.string.ussd_missing), StatusKind.ERROR)
            return
        }
        sendUssdButton.isEnabled = false
        showStatus(getString(R.string.ussd_running), StatusKind.LOADING)
        TelephonyGateway.sendUssd(this, code) { result ->
            runOnUiThread {
                sendUssdButton.isEnabled = true
                result.fold(
                    onSuccess = {
                        showStatus(getString(R.string.ussd_result, it), StatusKind.SUCCESS)
                    },
                    onFailure = {
                        showStatus(
                            getString(
                                R.string.ussd_failed,
                                it.message ?: getString(R.string.unknown_error),
                            ),
                            StatusKind.ERROR,
                        )
                    },
                )
            }
        }
    }

    private fun saveConfig() {
        val result = ConfigStore.save(this, configEditor.text.toString())
        result.fold(
            onSuccess = { showStatus(getString(R.string.config_saved), StatusKind.SUCCESS) },
            onFailure = {
                showStatus(
                    getString(R.string.config_invalid, it.message ?: getString(R.string.unknown_error)),
                    StatusKind.ERROR,
                )
            },
        )
    }

    private fun showStatus(message: String, kind: StatusKind) {
        statusText.text = message
        statusText.setTextColor(getColor(kind.textColor))
        statusText.setBackgroundResource(kind.background)
    }

    private fun requestRuntimePermissions() {
        val permissions = mutableListOf(
            Manifest.permission.RECEIVE_SMS,
            Manifest.permission.SEND_SMS,
            Manifest.permission.READ_PHONE_STATE,
            Manifest.permission.READ_CALL_LOG,
            Manifest.permission.CALL_PHONE,
        )
        if (Build.VERSION.SDK_INT >= 33) permissions += Manifest.permission.POST_NOTIFICATIONS
        val missing = permissions.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) requestPermissions(missing.toTypedArray(), REQUEST_PERMISSIONS)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQUEST_PERMISSIONS) return
        if (grantResults.any { it != PackageManager.PERMISSION_GRANTED }) {
            val message = getString(R.string.permission_warning)
            showStatus(message, StatusKind.LOADING)
            Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        }
    }

    private enum class StatusKind(val background: Int, val textColor: Int) {
        INFO(R.drawable.bg_status_info, R.color.omni_body),
        SUCCESS(R.drawable.bg_status_success, R.color.omni_success),
        ERROR(R.drawable.bg_status_error, R.color.omni_error),
        LOADING(R.drawable.bg_status_loading, R.color.omni_amber),
    }

    companion object {
        private const val REQUEST_PERMISSIONS = 10
    }
}
