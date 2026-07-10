package com.omnisms.app

import android.content.Context
import android.os.SystemClock
import androidx.work.BackoffPolicy
import androidx.work.Data
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

object OmniSmsProcessor {
    fun process(context: Context, sender: String, text: String, receiver: String = "") {
        val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
        val configJson = ConfigStore.load(context)
        if (processAdminCommand(context, configJson, sender, text)) return
        val planRaw = CoreBridge.nativePlanSms(
            configJson, sender, text, timestamp, receiver, System.currentTimeMillis(),
        )
        val plan = runCatching { JSONObject(planRaw) }.getOrElse {
            InboxStore.addReceived(context, sender, text, timestamp)
            return
        }
        val accepted = plan.optBoolean("accepted", false)
        val reason = plan.optString("reason", if (accepted) "accepted" else "unknown")
        if (!accepted && reason == "blacklist") return
        val messageId = InboxStore.addReceived(context, sender, text, timestamp)
        if (!accepted) {
            if (reason == "rule_drop") InboxStore.setForwarded(context, messageId, true)
            return
        }

        val requests = plan.optJSONArray("requests")
        val httpTargets = requests?.length() ?: 0
        val email = plan.optJSONObject("email")
        val totalTargets = httpTargets + if (email != null) 1 else 0
        if (totalTargets == 0) return
        ForwardCompletionStore.begin(context, messageId, totalTargets)
        if (email != null && !EmailDelivery.enqueue(context, email, messageId)) {
            ForwardCompletionStore.complete(context, messageId, false)
        }
        val workManager = WorkManager.getInstance(context)
        for (index in 0 until httpTargets) {
            val requestJson = requests?.optJSONObject(index)
            if (requestJson == null) {
                ForwardCompletionStore.complete(context, messageId, false)
                continue
            }
            requestJson.put("messageId", messageId)
            val id = PendingRequestStore.put(context, requestJson)
            val work = OneTimeWorkRequestBuilder<PushWorker>()
                .setInputData(
                    Data.Builder()
                        .putString(PushWorker.KEY_REQUEST_ID, id)
                        .putLong(PushWorker.KEY_MESSAGE_ID, messageId)
                        .build(),
                )
                .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 20, TimeUnit.SECONDS)
                .build()
            runCatching { workManager.enqueue(work) }
                .onFailure {
                    PendingRequestStore.remove(context, id)
                    ForwardCompletionStore.complete(context, messageId, false)
                }
        }
    }

    private fun processAdminCommand(
        context: Context,
        configJson: String,
        sender: String,
        text: String,
    ): Boolean {
        val raw = CoreBridge.nativeEvaluateAdmin(
            configJson, sender, text, SystemClock.elapsedRealtime() / 1000,
            adminSmsBusy.get(), false,
        )
        val decision = runCatching { JSONObject(raw) }.getOrNull() ?: return false
        if (!decision.optBoolean("handled", false)) return false
        when (decision.optString("action")) {
            "send_sms" -> {
                if (!adminSmsBusy.compareAndSet(false, true)) {
                    val message = "已有管理员短信任务在执行，请稍后重试"
                    AdminCommandNotifier.show(context, "命令执行失败", message)
                    return true
                }
                val target = decision.optString("target")
                val content = decision.optString("content")
                val result = TelephonyGateway.sendSms(context, target, content)
                adminSmsBusy.set(false)
                val ok = result.isSuccess
                InboxStore.addSent(context, target, content, ok)
                AdminCommandNotifier.show(
                    context, if (ok) "短信发送成功" else "短信发送失败",
                    if (ok) "已向 $target 提交短信" else (result.exceptionOrNull()?.message ?: "系统拒绝发送"),
                )
            }
            "reset" -> {
                // 普通 APK 无权重启 Android 设备，不能把重启前台服务伪装成设备 RESET。
                AdminCommandNotifier.show(context, "RESET 不受支持", "Android 普通应用无权重启设备")
            }
            else -> {
                val message = decision.optString("message", "管理员命令被拒绝")
                AdminCommandNotifier.show(context, "命令执行失败", message)
            }
        }
        return true
    }

    private val adminSmsBusy = AtomicBoolean(false)
}
