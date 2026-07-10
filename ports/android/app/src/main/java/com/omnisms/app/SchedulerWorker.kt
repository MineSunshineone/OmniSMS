package com.omnisms.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.SystemClock
import androidx.work.BackoffPolicy
import androidx.work.Data
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class SchedulerWorker(context: Context, params: WorkerParameters) : Worker(context, params) {
    override fun doWork(): Result {
        val configJson = ConfigStore.load(applicationContext)
        val config = runCatching { JSONObject(configJson) }.getOrElse { return Result.failure() }
        val state = SchedulerStateStore(applicationContext)
        val now = System.currentTimeMillis() / 1000
        val receiver = config.optString("receiver")

        val previousCheck = state.lastPeriodicCheck()
        if (previousCheck == 0L || now < previousCheck || now - previousCheck >= 3600) {
            state.setLastPeriodicCheck(now)
            var actionRan = false
            val keepalive = config.optJSONObject("keepalive")
            if (keepalive != null) {
                val configuredLast = keepalive.optLong("lastRun", 0)
                val last = state.keepaliveLast(configuredLast)
                when (CoreBridge.nativeEvaluatePeriodic(
                    keepalive.optBoolean("enabled"), last, now,
                    keepalive.optInt("intervalDays", 175),
                )) {
                    PERIODIC_ESTABLISH -> state.setKeepaliveLast(now)
                    PERIODIC_DUE -> {
                        val result = runAction(
                            configJson = configJson,
                            action = keepalive.optInt("action", 1),
                            target = keepalive.optString("target"),
                            payload = "",
                            url = keepalive.optString("url"),
                            profile = keepalive.optString("profile"),
                            label = "保号任务",
                            keepalive = true,
                            receiver = receiver,
                        )
                        actionRan = true
                        if (result.ok) {
                            state.setKeepaliveLast(now)
                            dispatchNotification(configJson, "保号动作已执行", result.message, receiver)
                        } else if (keepalive.optString("profile").isNotEmpty()) {
                            state.setKeepaliveLast(
                                CoreBridge.nativeFailureRetryBaseline(
                                    now, keepalive.optInt("intervalDays", 175),
                                ),
                            )
                        }
                        AdminCommandNotifier.show(
                            applicationContext,
                            if (result.ok) "保号动作成功" else "保号动作失败",
                            result.message,
                        )
                    }
                }
            }

            if (!actionRan) {
                val tasks = config.optJSONArray("scheduledTasks")
                if (tasks != null) {
                    for (index in 0 until minOf(tasks.length(), 6)) {
                        val task = tasks.optJSONObject(index) ?: continue
                        val days = task.optInt("intervalDays", 30)
                        val last = state.taskLast(index, task.optLong("lastRun", 0))
                        when (CoreBridge.nativeEvaluatePeriodic(
                            task.optBoolean("enabled"), last, now, days,
                        )) {
                            PERIODIC_ESTABLISH -> state.setTaskLast(index, now)
                            PERIODIC_DUE -> {
                                val label = task.optString("name").ifEmpty { "定时任务${index + 1}" }
                                val result = runAction(
                                    configJson, task.optInt("action", 0), task.optString("target"),
                                    task.optString("payload"), "", task.optString("profile"),
                                    label, false, receiver,
                                )
                                state.setTaskLast(
                                    index,
                                    if (result.ok) now else CoreBridge.nativeFailureRetryBaseline(now, days),
                                )
                                if (task.optInt("action", 0) != 0 || !result.ok) {
                                    dispatchNotification(
                                        configJson,
                                        if (result.ok) "定时任务已执行" else "定时任务失败",
                                        "任务: $label\n结果: ${result.message}",
                                        receiver,
                                    )
                                }
                                break
                            }
                        }
                    }
                }
            }
        }

        val time = config.optJSONObject("time")
        if (time != null) {
            val timezone = time.optInt("timezoneOffsetMin", 480)
            val heartbeatDay = CoreBridge.nativeDailyDueDay(
                time.optBoolean("heartbeatEnabled"), time.optInt("heartbeatHour", 9),
                state.heartbeatLastDay(), now, timezone,
            )
            if (heartbeatDay != NOT_DUE) {
                state.setHeartbeatLastDay(heartbeatDay)
                dispatchNotification(configJson, "设备每日心跳", "Android OmniSMS 运行正常", receiver)
            }

            val rebootDay = CoreBridge.nativeDailyRebootDueDay(
                time.optBoolean("rebootEnabled"), time.optInt("rebootHour", 4),
                state.rebootLastDay(), now, timezone,
                SystemClock.elapsedRealtime() / 1000, true,
            )
            if (rebootDay != NOT_DUE) {
                state.setRebootLastDay(rebootDay)
                // 普通应用没有 REBOOT 系统权限，明确通知一次并按自然日去重。
                AdminCommandNotifier.show(
                    applicationContext, "定时重启不受支持",
                    "Android 普通应用无权重启整台设备；前台服务继续运行",
                )
            }
        }
        return Result.success()
    }

    private fun runAction(
        configJson: String,
        action: Int,
        target: String,
        payload: String,
        url: String,
        profile: String,
        label: String,
        keepalive: Boolean,
        receiver: String,
    ): ActionResult {
        if (profile.isNotEmpty()) {
            return ActionResult(false, "Android 普通应用无权切换指定 eSIM Profile")
        }
        if (action == 0 && !keepalive) {
            val body = payload.ifEmpty { "定时提醒触发：$label" }
            dispatchNotification(configJson, label, body, receiver)
            return ActionResult(true, "提醒已入队")
        }
        if (action == 1 || (keepalive && action != 2 && action != 3)) {
            val requestUrl = (if (url.isNotEmpty()) url else target).ifEmpty {
                "http://gg.incrafttime.top/api/payload?size=64342"
            }
            return cellularHttpGet(requestUrl)
        }
        if (action == 2) {
            val body = if (keepalive) "keepalive" else payload.ifEmpty { "scheduled task" }
            val result = TelephonyGateway.sendSms(applicationContext, target, body)
            InboxStore.addSent(applicationContext, target, body, result.isSuccess)
            return ActionResult(result.isSuccess, result.exceptionOrNull()?.message ?: "短信已提交")
        }
        if (action == 3) return sendUssdBlocking(target)
        return ActionResult(false, "未知调度动作")
    }

    private fun cellularHttpGet(url: String): ActionResult {
        val manager = applicationContext.getSystemService(ConnectivityManager::class.java)
        val latch = CountDownLatch(1)
        var selected: Network? = null
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                selected = network
                latch.countDown()
            }
            override fun onUnavailable() = latch.countDown()
        }
        return try {
            val request = NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build()
            manager.requestNetwork(request, callback, 20_000)
            latch.await(25, TimeUnit.SECONDS)
            val network = selected ?: return ActionResult(false, "未获得蜂窝数据网络")
            val connection = network.openConnection(URL(url)) as HttpURLConnection
            try {
                connection.requestMethod = "GET"
                connection.connectTimeout = 30_000
                connection.readTimeout = 30_000
                val status = connection.responseCode
                ActionResult(status in 200..299, "HTTP $status")
            } finally {
                connection.disconnect()
            }
        } catch (error: Exception) {
            ActionResult(false, error.message ?: "蜂窝 HTTP 失败")
        } finally {
            runCatching { manager.unregisterNetworkCallback(callback) }
        }
    }

    private fun sendUssdBlocking(code: String): ActionResult {
        val latch = CountDownLatch(1)
        var result = ActionResult(false, "USSD 超时")
        TelephonyGateway.sendUssd(applicationContext, code) {
            result = ActionResult(it.isSuccess, it.getOrElse { error -> error.message ?: "USSD 失败" })
            latch.countDown()
        }
        latch.await(30, TimeUnit.SECONDS)
        return result
    }

    private fun dispatchNotification(
        configJson: String,
        title: String,
        body: String,
        receiver: String,
    ) {
        AdminCommandNotifier.show(applicationContext, title, body)
        val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
        val raw = CoreBridge.nativePlanNotification(
            configJson, title, body, timestamp, receiver, System.currentTimeMillis(),
        )
        val plan = runCatching { JSONObject(raw) }.getOrNull() ?: return
        val requests = plan.optJSONArray("requests")
        val manager = WorkManager.getInstance(applicationContext)
        if (requests != null) {
            for (index in 0 until requests.length()) {
                val request = requests.optJSONObject(index) ?: continue
                val id = PendingRequestStore.put(applicationContext, request)
                val work = OneTimeWorkRequestBuilder<PushWorker>()
                    .setInputData(
                        Data.Builder().putString(PushWorker.KEY_REQUEST_ID, id)
                            .putLong(PushWorker.KEY_MESSAGE_ID, 0).build(),
                    )
                    .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 20, TimeUnit.SECONDS)
                    .build()
                runCatching { manager.enqueue(work) }
                    .onFailure { PendingRequestStore.remove(applicationContext, id) }
            }
        }
        plan.optJSONObject("email")?.let { EmailDelivery.enqueue(applicationContext, it, 0) }
    }

    private data class ActionResult(val ok: Boolean, val message: String)

    companion object {
        private const val PERIODIC_ESTABLISH = 2
        private const val PERIODIC_DUE = 4
        private const val NOT_DUE = Long.MIN_VALUE
    }
}
