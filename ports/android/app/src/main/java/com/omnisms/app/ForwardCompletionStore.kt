package com.omnisms.app

import android.content.Context
import org.json.JSONObject

object ForwardCompletionStore {
    private const val PREFS = "omnisms_forward_completion"

    @Synchronized
    fun begin(context: Context, messageId: Long, targets: Int) {
        if (messageId <= 0 || targets <= 0) return
        val state = JSONObject().put("remaining", targets).put("failed", false)
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putString(messageId.toString(), state.toString()).commit()
    }

    @Synchronized
    fun complete(context: Context, messageId: Long, success: Boolean) {
        if (messageId <= 0) return
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val key = messageId.toString()
        val state = runCatching { JSONObject(prefs.getString(key, null) ?: return) }.getOrNull() ?: return
        val remaining = state.optInt("remaining", 0) - 1
        val failed = state.optBoolean("failed", false) || !success
        if (remaining <= 0) {
            prefs.edit().remove(key).commit()
            InboxStore.setForwarded(context, messageId, !failed)
        } else {
            state.put("remaining", remaining).put("failed", failed)
            prefs.edit().putString(key, state.toString()).commit()
        }
    }
}
