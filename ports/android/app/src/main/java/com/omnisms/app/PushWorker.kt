package com.omnisms.app

import android.content.Context
import androidx.work.Worker
import androidx.work.WorkerParameters
import java.net.HttpURLConnection
import java.net.URL

class PushWorker(context: Context, params: WorkerParameters) : Worker(context, params) {
    override fun doWork(): Result {
        val id = inputData.getString(KEY_REQUEST_ID) ?: return Result.failure()
        val messageId = inputData.getLong(KEY_MESSAGE_ID, 0)
        val request = PendingRequestStore.get(applicationContext, id)
        if (request == null) {
            ForwardCompletionStore.complete(applicationContext, messageId, false)
            return Result.failure()
        }
        if (runAttemptCount >= MAX_ATTEMPTS) {
            PendingRequestStore.remove(applicationContext, id)
            ForwardCompletionStore.complete(applicationContext, messageId, false)
            return Result.failure()
        }
        val connection = runCatching {
            (URL(request.getString("url")).openConnection() as HttpURLConnection).apply {
                requestMethod = request.optString("method", "POST")
                connectTimeout = request.optInt("timeoutMs", 10_000)
                readTimeout = request.optInt("timeoutMs", 10_000)
                setRequestProperty("Content-Type", request.optString("contentType", "application/json"))
                setRequestProperty("User-Agent", "OmniSMS-Android/0.1")
                val body = request.optString("body", "")
                if (requestMethod != "GET" && body.isNotEmpty()) {
                    doOutput = true
                    outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                }
            }
        }.getOrElse { return Result.retry() }

        return try {
            val status = connection.responseCode
            if (status in 200..299) {
                PendingRequestStore.remove(applicationContext, id)
                ForwardCompletionStore.complete(applicationContext, messageId, true)
                Result.success()
            } else if (status in 400..499 && status != 408 && status != 429) {
                PendingRequestStore.remove(applicationContext, id)
                ForwardCompletionStore.complete(applicationContext, messageId, false)
                Result.failure()
            } else {
                Result.retry()
            }
        } catch (_: Exception) {
            Result.retry()
        } finally {
            connection.disconnect()
        }
    }

    companion object {
        const val KEY_REQUEST_ID = "request_id"
        const val KEY_MESSAGE_ID = "message_id"
        private const val MAX_ATTEMPTS = 6
    }
}
