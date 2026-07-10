package com.omnisms.app

object CoreBridge {
    init {
        System.loadLibrary("omnisms_android")
    }

    external fun nativeValidateConfig(json: String): String
    external fun nativeCanonicalPhone(phone: String): String
    external fun nativeInboxClear()
    external fun nativeInboxRestoreReceived(
        id: Long, epoch: Long, sender: String, timestamp: String, text: String, forwarded: Boolean,
    )
    external fun nativeInboxRestoreSent(id: Long, epoch: Long, target: String, text: String, ok: Boolean)
    external fun nativeInboxAddReceived(sender: String, text: String, timestamp: String, epoch: Long): Long
    external fun nativeInboxAddSent(target: String, text: String, ok: Boolean, epoch: Long)
    external fun nativeInboxSetForwarded(id: Long, forwarded: Boolean): Boolean
    external fun nativeInboxDelete(id: Long): Boolean
    external fun nativeInboxJson(sentBox: Boolean, limit: Int): String
    external fun nativeEvaluateAdmin(
        configJson: String,
        sender: String,
        text: String,
        uptimeSeconds: Long,
        smsBusy: Boolean,
        resetPending: Boolean,
    ): String
    external fun nativeEvaluatePeriodic(
        enabled: Boolean, lastEpoch: Long, nowEpoch: Long, intervalDays: Int,
    ): Int
    external fun nativeFailureRetryBaseline(nowEpoch: Long, intervalDays: Int): Long
    external fun nativeDailyDueDay(
        enabled: Boolean, targetHour: Int, lastDay: Long, nowEpoch: Long, timezoneOffsetMin: Int,
    ): Long
    external fun nativeDailyRebootDueDay(
        enabled: Boolean, targetHour: Int, lastDay: Long, nowEpoch: Long,
        timezoneOffsetMin: Int, uptimeSeconds: Long, systemIdle: Boolean,
    ): Long
    external fun nativePlanNotification(
        configJson: String,
        title: String,
        body: String,
        timestamp: String,
        receiver: String,
        nowEpochMs: Long,
    ): String
    external fun nativePlanSms(
        configJson: String,
        sender: String,
        text: String,
        timestamp: String,
        receiver: String,
        nowEpochMs: Long,
    ): String
}
