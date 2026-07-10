package com.omnisms.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.provider.Telephony

class SmsReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Telephony.Sms.Intents.SMS_RECEIVED_ACTION) return
        val messages = Telephony.Sms.Intents.getMessagesFromIntent(intent)
        messages.groupBy { it.originatingAddress.orEmpty() }.forEach { (sender, parts) ->
            val body = parts.sortedBy { it.timestampMillis }.joinToString("") { it.messageBody.orEmpty() }
            OmniSmsProcessor.process(context.applicationContext, sender, body)
        }
    }
}
