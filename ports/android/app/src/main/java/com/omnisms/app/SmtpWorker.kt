package com.omnisms.app

import android.content.Context
import androidx.work.Worker
import androidx.work.WorkerParameters
import org.json.JSONObject

class SmtpWorker(context: Context, params: WorkerParameters) : Worker(context, params) {
    override fun doWork(): Result {
        val id = inputData.getString(KEY_REQUEST_ID) ?: return Result.failure()
        val messageId = inputData.getLong(KEY_MESSAGE_ID, 0)
        val request = PendingRequestStore.get(applicationContext, id)
        if (request == null) {
            ForwardCompletionStore.complete(applicationContext, messageId, false)
            return Result.failure()
        }
        if (runAttemptCount >= MAX_ATTEMPTS) {
            finish(id, messageId, false)
            return Result.failure()
        }
        val email = runCatching {
            JSONObject(ConfigStore.load(applicationContext)).getJSONObject("email")
        }.getOrElse {
            finish(id, messageId, false)
            return Result.failure()
        }
        if (!email.optBoolean("enabled", false)) {
            finish(id, messageId, false)
            return Result.failure()
        }
        val settings = SmtpSettings(
            server = email.optString("server"),
            port = email.optInt("port", 465),
            username = email.optString("username"),
            password = email.optString("password"),
            recipient = email.optString("recipient"),
        )
        return when (SmtpClient.send(
            settings, request.optString("subject"), request.optString("body"),
        )) {
            SmtpOutcome.SUCCESS -> {
                finish(id, messageId, true)
                Result.success()
            }
            SmtpOutcome.PERMANENT_FAILURE -> {
                finish(id, messageId, false)
                Result.failure()
            }
            SmtpOutcome.RETRY -> Result.retry()
        }
    }

    private fun finish(id: String, messageId: Long, success: Boolean) {
        PendingRequestStore.remove(applicationContext, id)
        ForwardCompletionStore.complete(applicationContext, messageId, success)
    }

    companion object {
        const val KEY_REQUEST_ID = "request_id"
        const val KEY_MESSAGE_ID = "message_id"
        private const val MAX_ATTEMPTS = 6
    }
}
