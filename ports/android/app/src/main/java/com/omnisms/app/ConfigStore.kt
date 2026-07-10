package com.omnisms.app

import android.content.Context

object ConfigStore {
    private const val PREFS = "omnisms_config"
    private const val KEY_JSON = "schema_v1_json"

    fun load(context: Context): String {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return prefs.getString(KEY_JSON, null) ?: context.assets.open("default_config.json")
            .bufferedReader(Charsets.UTF_8).use { it.readText() }
    }

    fun save(context: Context, json: String): Result<Unit> {
        val error = CoreBridge.nativeValidateConfig(json)
        if (error.isNotEmpty()) return Result.failure(IllegalArgumentException(error))
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putString(KEY_JSON, json).apply()
        AndroidScheduler.ensureScheduled(context.applicationContext)
        return Result.success(Unit)
    }
}
