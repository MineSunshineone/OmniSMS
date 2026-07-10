package com.omnisms.app

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import androidx.core.app.NotificationCompat

object AdminCommandNotifier {
    private const val CHANNEL_ID = "omnisms_admin"

    fun show(context: Context, title: String, message: String) {
        val manager = context.getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "OmniSMS 管理员命令", NotificationManager.IMPORTANCE_DEFAULT),
        )
        manager.notify(
            (System.currentTimeMillis() and 0x7FFFFFFF).toInt(),
            NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.stat_notify_chat)
                .setContentTitle(title)
                .setContentText(message)
                .setStyle(NotificationCompat.BigTextStyle().bigText(message))
                .setAutoCancel(true)
                .build(),
        )
    }
}
