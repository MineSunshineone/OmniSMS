package com.omnisms.app

import android.content.Context

class SchedulerStateStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun keepaliveLast(defaultValue: Long): Long = prefs.getLong(KEY_KEEPALIVE, defaultValue)
    fun setKeepaliveLast(value: Long) = prefs.edit().putLong(KEY_KEEPALIVE, value).commit()
    fun taskLast(index: Int, defaultValue: Long): Long = prefs.getLong("task_$index", defaultValue)
    fun setTaskLast(index: Int, value: Long) = prefs.edit().putLong("task_$index", value).commit()
    fun heartbeatLastDay(): Long = prefs.getLong(KEY_HEARTBEAT_DAY, -1)
    fun setHeartbeatLastDay(value: Long) = prefs.edit().putLong(KEY_HEARTBEAT_DAY, value).commit()
    fun rebootLastDay(): Long = prefs.getLong(KEY_REBOOT_DAY, -1)
    fun setRebootLastDay(value: Long) = prefs.edit().putLong(KEY_REBOOT_DAY, value).commit()
    fun lastPeriodicCheck(): Long = prefs.getLong(KEY_LAST_CHECK, 0)
    fun setLastPeriodicCheck(value: Long) = prefs.edit().putLong(KEY_LAST_CHECK, value).commit()

    companion object {
        private const val PREFS = "omnisms_scheduler_state"
        private const val KEY_KEEPALIVE = "keepalive_last"
        private const val KEY_HEARTBEAT_DAY = "heartbeat_last_day"
        private const val KEY_REBOOT_DAY = "reboot_last_day"
        private const val KEY_LAST_CHECK = "last_periodic_check"
    }
}
