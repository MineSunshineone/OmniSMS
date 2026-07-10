package com.omnisms.app

import android.content.Context
import org.json.JSONObject
import java.util.UUID

object PendingRequestStore {
    private const val PREFS = "omnisms_pending_http"

    @Synchronized
    fun put(context: Context, request: JSONObject): String {
        val id = UUID.randomUUID().toString()
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putString(id, request.toString()).commit()
        return id
    }

    fun get(context: Context, id: String): JSONObject? {
        val raw = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(id, null) ?: return null
        return runCatching { JSONObject(raw) }.getOrNull()
    }

    fun remove(context: Context, id: String) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(id).apply()
    }
}
