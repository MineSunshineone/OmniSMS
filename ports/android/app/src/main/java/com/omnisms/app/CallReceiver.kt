package com.omnisms.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.telephony.TelephonyManager

class CallReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != TelephonyManager.ACTION_PHONE_STATE_CHANGED) return
        if (intent.getStringExtra(TelephonyManager.EXTRA_STATE) != TelephonyManager.EXTRA_STATE_RINGING) return
        @Suppress("DEPRECATION")
        val number = intent.getStringExtra(TelephonyManager.EXTRA_INCOMING_NUMBER).orEmpty()
        schedule(context.applicationContext, number)
    }

    companion object {
        private val handler = Handler(Looper.getMainLooper())
        private var candidate = ""
        private var pending: Runnable? = null

        @Synchronized
        private fun schedule(context: Context, number: String) {
            if (number.isNotEmpty()) candidate = number
            pending?.let(handler::removeCallbacks)
            val task = Runnable {
                val display = synchronized(this) {
                    candidate.ifEmpty { "未知号码" }.also { candidate = "" }
                }
                OmniSmsProcessor.process(context, display, "来电：$display")
            }
            pending = task
            handler.postDelayed(task, 500)
        }
    }
}
