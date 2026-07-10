package com.omnisms.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        AndroidScheduler.ensureScheduled(context.applicationContext)
        val service = Intent(context, OmniSmsService::class.java)
        context.startForegroundService(service)
    }
}
