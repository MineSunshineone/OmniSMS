package com.omnisms.app

import android.content.Context
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import java.util.concurrent.TimeUnit

object AndroidScheduler {
    private const val WORK_NAME = "omnisms_periodic_scheduler"

    fun ensureScheduled(context: Context) {
        val work = PeriodicWorkRequestBuilder<SchedulerWorker>(15, TimeUnit.MINUTES).build()
        WorkManager.getInstance(context).enqueueUniquePeriodicWork(
            WORK_NAME, ExistingPeriodicWorkPolicy.UPDATE, work,
        )
    }
}
