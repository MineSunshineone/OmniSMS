package com.omnisms.app

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Data
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import org.json.JSONObject
import java.util.concurrent.TimeUnit

object EmailDelivery {
    fun enqueue(context: Context, email: JSONObject, messageId: Long): Boolean {
        val request = JSONObject()
            .put("kind", "smtp")
            .put("subject", email.optString("subject"))
            .put("body", email.optString("body"))
        val id = PendingRequestStore.put(context, request)
        val work = OneTimeWorkRequestBuilder<SmtpWorker>()
            .setInputData(
                Data.Builder()
                    .putString(SmtpWorker.KEY_REQUEST_ID, id)
                    .putLong(SmtpWorker.KEY_MESSAGE_ID, messageId)
                    .build(),
            )
            .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 20, TimeUnit.SECONDS)
            .build()
        return runCatching { WorkManager.getInstance(context).enqueue(work) }
            .onFailure { PendingRequestStore.remove(context, id) }
            .isSuccess
    }
}
